#!/bin/bash
# Checks the androidNativeComponents pins and writes their SHA256SUMS to the file in $1. The release tag
# is android-native-<sha8>, sha8 the start of the SHA-256 of SHA256SUMS; the asset of <profile>/<component>
# is <profile>-<component>-<output>, every pinned URL must name its asset in that release of this
# repository, and a tag run must be that tag. Run from the checkout root.
set -euo pipefail
sums=${1:?SHA256SUMS path required}
pins=src/app/toolchain.json
rows=$(jq -er '.androidNativeComponents | to_entries[]
    | [.key, ((.value.sha256 | strings) // error("missing sha256")),
       ((.value.url | strings) // error("missing url"))] | @tsv' "$pins")
lines=
urls=
while IFS=$'\t' read -r key sha url; do
    [[ "$key" =~ ^[a-z0-9][a-z0-9.-]*/[a-z0-9][a-z0-9-]*$ ]] || { echo "Invalid component $key" >&2; exit 1; }
    [[ "$sha" =~ ^[0-9a-f]{64}$ ]] || { echo "Invalid SHA-256 for $key" >&2; exit 1; }
    asset=${url##*/}
    [[ "$asset" == "${key/\//-}-"* && "$asset" =~ ^[A-Za-z0-9_.-]+$ ]] || { echo "$key URL $url does not name its asset" >&2; exit 1; }
    lines+="$sha  $asset"$'\n'
    urls+="$url"$'\n'
done <<< "$rows"
[[ -n "$lines" ]] || { echo "No androidNativeComponents pins" >&2; exit 1; }
set_sha=$(printf '%s' "$lines" | sha256sum)
tag="android-native-${set_sha:0:8}"
base="https://github.com/$GITHUB_REPOSITORY/releases/download/$tag/"
while read -r sha asset <&3 && read -r url <&4; do
    [[ "$url" == "$base$asset" ]] || { echo "$asset URL $url does not name $base$asset" >&2; exit 1; }
done 3<<< "${lines%$'\n'}" 4<<< "${urls%$'\n'}"
if [[ "$GITHUB_REF_TYPE" == tag && "$GITHUB_REF_NAME" != "$tag" ]]; then
    echo "Tag $GITHUB_REF_NAME differs from the pinned native components $tag" >&2
    exit 1
fi
mkdir -p "$(dirname "$sums")"
printf '%s' "$lines" > "$sums"
echo "tag=$tag" >> "$GITHUB_OUTPUT"
