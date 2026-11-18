#!/bin/bash
# Run from the checkout root before installing or building kernel inputs.
set -euo pipefail
if [[ "$GITHUB_REF_TYPE" == tag ]]; then
    [[ "$GITHUB_REF_NAME" =~ ^kernel-(qinggan-(09|11))-([0-9a-f]{8})$ ]] || { echo "Unexpected tag $GITHUB_REF_NAME" >&2; exit 1; }
    kernel=${BASH_REMATCH[1]}
else
    kernel=$INPUT_KERNEL
fi
case "$kernel" in
    qinggan-09) profiles=(free-09-chinese-20260106-6.0.3) ;;
    qinggan-11) profiles=(free-11-chinese-20260731-7.3.1 free-11-chinese-20250710-6.11.1) ;;
    *) echo "Unknown kernel $kernel" >&2; exit 1 ;;
esac
base=$(jq -er '.optionalTools.kernelBuild.releaseBase | strings' src/app/toolchain.json)
[[ "$base" == "https://github.com/voyemu/voyemu-deps/releases/download/" ]] || { echo "Invalid dependency release base: $base" >&2; exit 1; }
sha=
for profile in "${profiles[@]}"; do
    pin=$(jq -er '.android.development_kernel.sha256 | strings' "profiles/$profile/profile.json")
    [[ "$pin" =~ ^[0-9a-f]{64}$ ]] || { echo "Invalid SHA-256 in $profile" >&2; exit 1; }
    if [[ -z "$sha" ]]; then
        sha=$pin
    fi
    [[ "$pin" == "$sha" ]] || { echo "Profile $profile differs from the $kernel Image pin" >&2; exit 1; }
    tag="kernel-$kernel-${sha:0:8}"
    url=$(jq -er '.android.development_kernel.url | strings' "profiles/$profile/profile.json")
    [[ "$url" == "$base$tag/Image" ]] || { echo "Profile $profile URL $url does not name $tag" >&2; exit 1; }
done
if [[ "$GITHUB_REF_TYPE" == tag && "$GITHUB_REF_NAME" != "$tag" ]]; then
    echo "Tag $GITHUB_REF_NAME differs from the profile pin $tag" >&2
    exit 1
fi
{
    echo "kernel=$kernel"
    echo "profile=${profiles[0]}"
    echo "sha=$sha"
    echo "tag=$tag"
} >> "$GITHUB_OUTPUT"
