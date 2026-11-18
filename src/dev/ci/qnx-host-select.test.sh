#!/bin/bash
set -euo pipefail
# Run the real CI selector against isolated JSON manifests; nothing is downloaded or built.
project=$(cd "$(dirname "$0")/../../.." && pwd -P)
selector="$project/src/dev/ci/qnx-host-select.sh"
mkdir -p "$project/.local/build/jobs"
work=$(mktemp -d "$project/.local/build/jobs/qnx-host-select.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
cd "$work"
export GITHUB_REPOSITORY GITHUB_REF_TYPE GITHUB_REF_NAME INPUT_HOST
export GITHUB_OUTPUT="$work/output"
sha=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
tag=qnx-host-macos-arm64-aaaaaaaa
url="https://github.com/voyemu/voyemu/releases/download/$tag/qnx-host-macos-arm64.zip"
# Writes the manifest from the variables that each case changes after reset.
write() {
    mkdir -p src/app
    printf '{"schema":1,"prebuilt":{"macos_arm64":%s,"linux_x64":%s,"windows_file_backend":%s}}\n' "$mac" "$linux" "$windows" > src/app/qnx-host.json
}
reset() {
    GITHUB_REF_TYPE=branch GITHUB_REF_NAME=main GITHUB_REPOSITORY=voyemu/voyemu INPUT_HOST=macos-arm64
    mac="{\"url\":\"$url\",\"sha256\":\"$sha\"}"
    linux=null
    windows=null
    : > "$GITHUB_OUTPUT"
}
# Accepts a case whose outputs name the expected engine, SHA-256 and tag.
accept() {
    write
    bash "$selector"
    grep -qx "host=$2" "$GITHUB_OUTPUT"
    grep -qx "sha=$3" "$GITHUB_OUTPUT"
    grep -qx "tag=$4" "$GITHUB_OUTPUT"
    grep -qx "asset=qnx-host-$2.zip" "$GITHUB_OUTPUT"
    echo "PASS $1"
}
reject() {
    write
    if bash "$selector" > rejected.log 2>&1; then
        echo "FAIL accepted $1" >&2
        exit 1
    fi
    test ! -s "$GITHUB_OUTPUT"
    echo "PASS reject $1"
}
reset
accept manual-pinned macos-arm64 "$sha" "$tag"
reset
GITHUB_REF_TYPE=tag GITHUB_REF_NAME=$tag
accept tag macos-arm64 "$sha" "$tag"
reset
INPUT_HOST=windows-x64
accept manual-pending windows-x64 "" qnx-host-windows-x64
grep -qx "runner=windows-2022" "$GITHUB_OUTPUT"
reset
INPUT_HOST=linux-x64
accept manual-pending-linux linux-x64 "" qnx-host-linux-x64
grep -qx "runner=ubuntu-24.04" "$GITHUB_OUTPUT"
grep -qx "toolchain=zig-linux-x64" "$GITHUB_OUTPUT"
reset
GITHUB_REF_TYPE=tag GITHUB_REF_NAME=qnx-host-linux-x64-aaaaaaaa
linux="{\"url\":\"https://github.com/voyemu/voyemu/releases/download/qnx-host-linux-x64-aaaaaaaa/qnx-host-linux-x64.zip\",\"sha256\":\"$sha\"}"
accept tag-linux linux-x64 "$sha" qnx-host-linux-x64-aaaaaaaa
reset
GITHUB_REF_TYPE=tag GITHUB_REF_NAME=qnx-host-windows-x64-aaaaaaaa
reject tag-pending
reset
GITHUB_REF_TYPE=tag GITHUB_REF_NAME=qnx-host-macos-arm64-00000000
reject tag-pin-mismatch
reset
GITHUB_REF_TYPE=tag GITHUB_REF_NAME=qnx-host-linux-arm64-aaaaaaaa
reject unknown-tag
reset
INPUT_HOST=linux-arm64
reject unknown-host
reset
GITHUB_REPOSITORY=fork/voyemu
reject foreign-repository
reset
mac="{\"url\":\"https://example.invalid/qnx-host-macos-arm64.zip\",\"sha256\":\"$sha\"}"
reject foreign-url
reset
mac="{\"url\":\"$url\",\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}"
reject sha-drift
reset
mac="{\"url\":\"$url\",\"sha256\":\"aaaaaaaa\"}"
reject malformed-sha256
reset
mac="{\"url\":\"$url\"}"
reject missing-sha256
reset
mac="{\"sha256\":\"$sha\"}"
reject missing-url
