#!/bin/bash
# Checks the javaRuntime.assets pins and writes their SHA256SUMS to the file in $1. The release tag is
# java-runtime-<sha8>, sha8 the start of the SHA-256 of SHA256SUMS; the asset of a host is
# java-runtime-<host>.zip, every pinned URL must name its asset in that release of this repository, and a
# tag run must be that tag. Run from the checkout root.
set -euo pipefail
sums=${1:?SHA256SUMS path required}
pins=src/app/toolchain.json
lines=
for host in darwin-arm64 linux-x64 win32-x64; do
    sha=$(jq -er ".javaRuntime.assets.\"$host\".sha256 | strings" "$pins")
    [[ "$sha" =~ ^[0-9a-f]{64}$ ]] || { echo "Invalid SHA-256 for $host" >&2; exit 1; }
    lines+="$sha  java-runtime-$host.zip"$'\n'
done
set_sha=$(printf '%s' "$lines" | sha256sum)
tag="java-runtime-${set_sha:0:8}"
base="https://github.com/voyemu/voyemu-deps/releases/download/$tag/"
while read -r sha file; do
    host=${file#java-runtime-}
    host=${host%.zip}
    url=$(jq -er ".javaRuntime.assets.\"$host\".url | strings" "$pins")
    [[ "$url" == "$base$file" ]] || { echo "$host URL $url does not name $base$file" >&2; exit 1; }
done <<< "${lines%$'\n'}"
if [[ "$GITHUB_REF_TYPE" == tag && "$GITHUB_REF_NAME" != "$tag" ]]; then
    echo "Tag $GITHUB_REF_NAME differs from the pinned runtimes $tag" >&2
    exit 1
fi
mkdir -p "$(dirname "$sums")"
printf '%s' "$lines" > "$sums"
echo "tag=$tag" >> "$GITHUB_OUTPUT"
