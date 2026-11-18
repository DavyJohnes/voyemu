#!/bin/bash
# Checks the optionalTools.uncrustify.binaries pins and writes their SHA256SUMS to the file in $1.
# The release tag is uncrustify-<version>-<sha8>, sha8 the start of the SHA-256 of SHA256SUMS; every
# pinned URL must name its asset in that release of this repository, and a tag run must be that tag.
# Run from the checkout root before building.
set -euo pipefail
sums=${1:?SHA256SUMS path required}
pins=src/app/toolchain.json
version=$(jq -er '.optionalTools.uncrustify.version | strings' "$pins")
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Invalid Uncrustify version $version" >&2; exit 1; }
lines=
for host in darwin-arm64 linux-x64 win32-x64; do
    sha=$(jq -er ".optionalTools.uncrustify.binaries.\"$host\".sha256 | strings" "$pins")
    [[ "$sha" =~ ^[0-9a-f]{64}$ ]] || { echo "Invalid SHA-256 for $host" >&2; exit 1; }
    file="uncrustify-$version-$host"
    if [[ "$host" == win32-x64 ]]; then
        file+=.exe
    fi
    lines+="$sha  $file"$'\n'
done
set_sha=$(printf '%s' "$lines" | sha256sum)
tag="uncrustify-$version-${set_sha:0:8}"
base="https://github.com/voyemu/voyemu-deps/releases/download/$tag/"
while read -r sha file; do
    host=${file#"uncrustify-$version-"}
    host=${host%.exe}
    url=$(jq -er ".optionalTools.uncrustify.binaries.\"$host\".url | strings" "$pins")
    [[ "$url" == "$base$file" ]] || { echo "Uncrustify $host URL $url does not name $base$file" >&2; exit 1; }
done <<< "${lines%$'\n'}"
if [[ "$GITHUB_REF_TYPE" == tag && "$GITHUB_REF_NAME" != "$tag" ]]; then
    echo "Tag $GITHUB_REF_NAME differs from the pinned binaries $tag" >&2
    exit 1
fi
mkdir -p "$(dirname "$sums")"
printf '%s' "$lines" > "$sums"
{
    echo "version=$version"
    echo "tag=$tag"
} >> "$GITHUB_OUTPUT"
