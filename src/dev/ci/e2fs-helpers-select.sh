#!/bin/bash
# Checks the e2fsprogs.helpers pins and writes their SHA256SUMS to the file in $1. The release tag is
# e2fs-helpers-<sha8>, sha8 the start of the SHA-256 of SHA256SUMS; every pinned URL must name its asset
# in that release of this repository, and a tag run must be that tag. Run from the checkout root.
set -euo pipefail
sums=${1:?SHA256SUMS path required}
pins=src/app/toolchain.json
lines=
for host in darwin-arm64 linux-x64 win32-x64; do
    for helper in a9-offline-labels api30-root-layout; do
        sha=$(jq -er ".e2fsprogs.helpers.\"$host\".\"$helper\".sha256 | strings" "$pins")
        [[ "$sha" =~ ^[0-9a-f]{64}$ ]] || { echo "Invalid SHA-256 for $helper $host" >&2; exit 1; }
        lines+="$sha  $helper-$host"$'\n'
    done
done
set_sha=$(printf '%s' "$lines" | sha256sum)
tag="e2fs-helpers-${set_sha:0:8}"
base="https://github.com/voyemu/voyemu-deps/releases/download/$tag/"
while read -r sha file; do
    host=${file#a9-offline-labels-}
    host=${host#api30-root-layout-}
    helper=${file%-"$host"}
    url=$(jq -er ".e2fsprogs.helpers.\"$host\".\"$helper\".url | strings" "$pins")
    [[ "$url" == "$base$file" ]] || { echo "$helper $host URL $url does not name $base$file" >&2; exit 1; }
done <<< "${lines%$'\n'}"
if [[ "$GITHUB_REF_TYPE" == tag && "$GITHUB_REF_NAME" != "$tag" ]]; then
    echo "Tag $GITHUB_REF_NAME differs from the pinned helpers $tag" >&2
    exit 1
fi
mkdir -p "$(dirname "$sums")"
printf '%s' "$lines" > "$sums"
echo "tag=$tag" >> "$GITHUB_OUTPUT"
