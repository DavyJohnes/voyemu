#!/bin/bash
# Checks the qnxGuestModules pins and writes their SHA256SUMS to the file in $1. The release tag is
# qnx-guest-<sha8>, sha8 the start of the SHA-256 of SHA256SUMS; every asset is <adapter>-<file>, and
# every pinned URL must name its asset in that release of this repository, and a tag run must be
# that tag. Run from the checkout root.
set -euo pipefail
sums=${1:?SHA256SUMS path required}
pins=src/app/toolchain.json
rows=$(jq -er '.qnxGuestModules | to_entries[] | .key as $adapter | .value.files | to_entries[]
    | [$adapter, .key, ((.value.sha256 | strings) // error("missing sha256")),
       ((.value.url | strings) // error("missing url"))] | @tsv' "$pins")
lines=
urls=
while IFS=$'\t' read -r adapter file sha url; do
    [[ "$adapter" =~ ^[a-z0-9-]+$ && "$file" =~ ^[a-z0-9_.-]+$ ]] || { echo "Invalid module $adapter $file" >&2; exit 1; }
    [[ "$sha" =~ ^[0-9a-f]{64}$ ]] || { echo "Invalid SHA-256 for $adapter $file" >&2; exit 1; }
    lines+="$sha  $adapter-$file"$'\n'
    urls+="$url"$'\n'
done <<< "$rows"
[[ -n "$lines" ]] || { echo "No qnxGuestModules pins" >&2; exit 1; }
set_sha=$(printf '%s' "$lines" | sha256sum)
tag="qnx-guest-${set_sha:0:8}"
base="https://github.com/$GITHUB_REPOSITORY/releases/download/$tag/"
while read -r sha asset <&3 && read -r url <&4; do
    [[ "$url" == "$base$asset" ]] || { echo "$asset URL $url does not name $base$asset" >&2; exit 1; }
done 3<<< "${lines%$'\n'}" 4<<< "${urls%$'\n'}"
if [[ "$GITHUB_REF_TYPE" == tag && "$GITHUB_REF_NAME" != "$tag" ]]; then
    echo "Tag $GITHUB_REF_NAME differs from the pinned modules $tag" >&2
    exit 1
fi
mkdir -p "$(dirname "$sums")"
printf '%s' "$lines" > "$sums"
echo "tag=$tag" >> "$GITHUB_OUTPUT"
