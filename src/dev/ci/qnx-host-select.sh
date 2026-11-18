#!/bin/bash
# Selects the QNX engine of the qnx-host workflow and checks its prebuilt pin in src/app/qnx-host.json.
# A tag run builds the engine of tag qnx-host-<host>-<sha8>, whose pin must name the asset
# qnx-host-<host>.zip of that release of this repository. A manual run builds the engine of INPUT_HOST;
# while its pin is null the run only reports the built SHA-256 to pin. Run from the checkout root.
set -euo pipefail
if [[ "$GITHUB_REF_TYPE" == tag ]]; then
    [[ "$GITHUB_REF_NAME" =~ ^qnx-host-(macos-arm64|linux-x64|windows-x64)-[0-9a-f]{8}$ ]] || { echo "Unexpected tag $GITHUB_REF_NAME" >&2; exit 1; }
    host=${BASH_REMATCH[1]}
else
    host=${INPUT_HOST:-}
fi
case "$host" in
    macos-arm64) section=macos_arm64 runner=macos-15 set=macos fixtures=true toolchain=zig-macos-arm64 test=qnx_host_test ;;
    linux-x64) section=linux_x64 runner=ubuntu-24.04 set=linux fixtures=true toolchain=zig-linux-x64 test=qnx_host_test ;;
    windows-x64) section=windows_file_backend runner=windows-2022 set=windows fixtures=false toolchain=zig-windows-x64 test=qnx_host_windows_test ;;
    *) echo "Unknown QNX engine host $host" >&2; exit 1 ;;
esac
asset="qnx-host-$host.zip"
pin=$(jq -ec ".prebuilt.$section" src/app/qnx-host.json || true)
if [[ "$pin" == null ]]; then
    [[ "$GITHUB_REF_TYPE" != tag ]] || { echo "The $section prebuilt pin is pending; pin the asset of a manual run first" >&2; exit 1; }
    sha=
    tag="qnx-host-$host"
else
    sha=$(jq -er '.sha256 | strings' <<< "$pin") || { echo "Invalid $section prebuilt pin" >&2; exit 1; }
    [[ "$sha" =~ ^[0-9a-f]{64}$ ]] || { echo "Invalid SHA-256 in the $section prebuilt pin" >&2; exit 1; }
    tag="qnx-host-$host-${sha:0:8}"
    url=$(jq -er '.url | strings' <<< "$pin") || { echo "Invalid $section prebuilt pin" >&2; exit 1; }
    expected="https://github.com/voyemu/voyemu-deps/releases/download/$tag/$asset"
    [[ "$url" == "$expected" ]] || { echo "The $section prebuilt URL $url does not name $expected" >&2; exit 1; }
    if [[ "$GITHUB_REF_TYPE" == tag && "$GITHUB_REF_NAME" != "$tag" ]]; then
        echo "Tag $GITHUB_REF_NAME differs from the $section prebuilt pin $tag" >&2
        exit 1
    fi
fi
{
    echo "host=$host"
    echo "runner=$runner"
    echo "set=$set"
    echo "fixtures=$fixtures"
    echo "toolchain=$toolchain"
    echo "test=$test"
    echo "asset=$asset"
    echo "sha=$sha"
    echo "tag=$tag"
} >> "$GITHUB_OUTPUT"
