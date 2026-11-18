#!/bin/bash
# Checks the javaHelper pin and writes its SHA256SUMS to the file in $1. The release tag is
# java-helper-<sha8>, sha8 the start of the SHA-256 of SHA256SUMS; the asset is java-helper.zip, the
# pinned URL must name it in that release of this repository, and a tag run must be that tag. Run from
# the checkout root.
set -euo pipefail
sums=${1:?SHA256SUMS path required}
pins=src/app/toolchain.json
sha=$(jq -er '.javaHelper.sha256 | strings' "$pins")
[[ "$sha" =~ ^[0-9a-f]{64}$ ]] || { echo "Invalid SHA-256 for java-helper.zip" >&2; exit 1; }
lines="$sha  java-helper.zip"$'\n'
set_sha=$(printf '%s' "$lines" | sha256sum)
tag="java-helper-${set_sha:0:8}"
base="https://github.com/voyemu/voyemu-deps/releases/download/$tag/"
url=$(jq -er '.javaHelper.url | strings' "$pins")
[[ "$url" == "${base}java-helper.zip" ]] || { echo "URL $url does not name ${base}java-helper.zip" >&2; exit 1; }
if [[ "$GITHUB_REF_TYPE" == tag && "$GITHUB_REF_NAME" != "$tag" ]]; then
    echo "Tag $GITHUB_REF_NAME differs from the pinned Java helper $tag" >&2
    exit 1
fi
mkdir -p "$(dirname "$sums")"
printf '%s' "$lines" > "$sums"
echo "tag=$tag" >> "$GITHUB_OUTPUT"
