#!/bin/bash
# Checks the androidJava pin and writes its SHA256SUMS to the file in $1. The release tag is
# android-java-<sha8>, sha8 the start of the SHA-256 of SHA256SUMS; the asset is android-java.zip, the
# pinned URL must name it in that release of this repository, and a tag run must be that tag. Run from
# the checkout root.
set -euo pipefail
sums=${1:?SHA256SUMS path required}
pins=src/app/toolchain.json
sha=$(jq -er '.androidJava.sha256 | strings' "$pins")
[[ "$sha" =~ ^[0-9a-f]{64}$ ]] || { echo "Invalid SHA-256 for android-java.zip" >&2; exit 1; }
lines="$sha  android-java.zip"$'\n'
set_sha=$(printf '%s' "$lines" | sha256sum)
tag="android-java-${set_sha:0:8}"
base="https://github.com/$GITHUB_REPOSITORY/releases/download/$tag/"
url=$(jq -er '.androidJava.url | strings' "$pins")
[[ "$url" == "${base}android-java.zip" ]] || { echo "URL $url does not name ${base}android-java.zip" >&2; exit 1; }
if [[ "$GITHUB_REF_TYPE" == tag && "$GITHUB_REF_NAME" != "$tag" ]]; then
    echo "Tag $GITHUB_REF_NAME differs from the pinned guest Java $tag" >&2
    exit 1
fi
mkdir -p "$(dirname "$sums")"
printf '%s' "$lines" > "$sums"
echo "tag=$tag" >> "$GITHUB_OUTPUT"
