#!/bin/bash
set -euo pipefail
# Run the real CI selector against isolated JSON manifests; no kernel inputs are fetched.
project=$(cd "$(dirname "$0")/../../.." && pwd -P)
selector="$project/src/dev/ci/kernel-select.sh"
mkdir -p "$project/.local/build/jobs"
work=$(mktemp -d "$project/.local/build/jobs/kernel-select.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
cd "$work"
export GITHUB_REPOSITORY=voyemu/voyemu GITHUB_REF_TYPE=branch GITHUB_REF_NAME=main INPUT_KERNEL=qinggan-11
export GITHUB_OUTPUT="$work/output"
base=https://github.com/voyemu/voyemu/releases/download/
p09=free-09-chinese-20260106-6.0.3
p11=free-11-chinese-20260731-7.3.1
p611=free-11-chinese-20250710-6.11.1
sha09=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
sha11=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
write_profile() {
    mkdir -p "profiles/$1"
    jq -n --arg sha "$2" --arg url "$3" '{android:{development_kernel:{sha256:$sha,url:$url}}}' > "profiles/$1/profile.json"
}
reset() {
    GITHUB_REF_TYPE=branch GITHUB_REF_NAME=main INPUT_KERNEL=qinggan-11
    mkdir -p src/app
    jq -n --arg base "$base" '{optionalTools:{kernelBuild:{releaseBase:$base}}}' > src/app/toolchain.json
    write_profile "$p09" "$sha09" "${base}kernel-qinggan-09-aaaaaaaa/Image"
    write_profile "$p11" "$sha11" "${base}kernel-qinggan-11-bbbbbbbb/Image"
    write_profile "$p611" "$sha11" "${base}kernel-qinggan-11-bbbbbbbb/Image"
    : > "$GITHUB_OUTPUT"
}
accept() {
    bash "$selector"
    printf 'kernel=%s\nprofile=%s\nsha=%s\ntag=kernel-%s-%.8s\n' "$1" "$2" "$3" "$1" "$3" > expected
    cmp expected "$GITHUB_OUTPUT"
    echo "PASS $4"
}
reject() {
    if bash "$selector" > rejected.log 2>&1; then
        echo "FAIL accepted $1" >&2
        exit 1
    fi
    test ! -s "$GITHUB_OUTPUT"
    echo "PASS reject $1"
}
reset
accept qinggan-11 "$p11" "$sha11" manual-11
reset
write_profile "$p611" bbbbbbbbbaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa "${base}kernel-qinggan-11-bbbbbbbb/Image"
reject 6.11.1-pin-drift
reset
write_profile "$p611" "$sha11" 'https://example.invalid/Image'
reject 6.11.1-url-drift
reset
INPUT_KERNEL=qinggan-09
accept qinggan-09 "$p09" "$sha09" manual-09
reset
GITHUB_REF_TYPE=tag GITHUB_REF_NAME=kernel-qinggan-11-bbbbbbbb INPUT_KERNEL=qinggan-09
accept qinggan-11 "$p11" "$sha11" tag-overrides-input
reset
GITHUB_REF_TYPE=tag GITHUB_REF_NAME=kernel-qinggan-09-aaaaaaaa
accept qinggan-09 "$p09" "$sha09" tag-09
reset
GITHUB_REF_TYPE=tag GITHUB_REF_NAME=kernel-qinggan-11-aaaaaaaa
reject tag-pin-mismatch
reset
GITHUB_REF_TYPE=tag GITHUB_REF_NAME=kernel-qinggan-11-invalid
reject malformed-tag
reset
INPUT_KERNEL=unknown
reject unknown-kernel
reset
printf '{' > "profiles/$p611/profile.json"
reject malformed-manifest
reset
rm "profiles/$p611/profile.json"
reject missing-manifest
for field in sha256 url; do
    reset
    jq "del(.android.development_kernel.$field)" "profiles/$p611/profile.json" > changed
    mv changed "profiles/$p611/profile.json"
    reject "missing-$field"
done
reset
write_profile "$p11" bbbbbbbb "${base}kernel-qinggan-11-bbbbbbbb/Image"
write_profile "$p611" bbbbbbbb "${base}kernel-qinggan-11-bbbbbbbb/Image"
reject malformed-sha256
reset
write_profile "$p09" "$sha09" 'https://example.invalid/Image'
INPUT_KERNEL=qinggan-09
reject 09-url-drift
reset
printf '{"optionalTools":{"kernelBuild":{"releaseBase":"https://example.invalid/"}}}\n' > src/app/toolchain.json
reject foreign-release-base
reset
printf '{}\n' > src/app/toolchain.json
reject missing-release-base
