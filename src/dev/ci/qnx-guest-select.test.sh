#!/bin/bash
set -euo pipefail
# Run the real CI selector against isolated JSON manifests; nothing is downloaded or built.
project=$(cd "$(dirname "$0")/../../.." && pwd -P)
selector="$project/src/dev/ci/qnx-guest-select.sh"
mkdir -p "$project/.local/build/jobs"
work=$(mktemp -d "$project/.local/build/jobs/qnx-guest-select.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
cd "$work"
export GITHUB_REPOSITORY=voyemu/voyemu GITHUB_REF_TYPE=branch GITHUB_REF_NAME=main
export GITHUB_OUTPUT="$work/output"
a=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
b=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
c=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc
printf '%s  %s\n' "$a" sa8155-qinggan09-front.so "$b" sa8155-qinggan09-readstub.bin \
    "$c" sa8155-passion-21480-front.so > expected-sums
set_sha=$(sha256sum < expected-sums)
tag="qnx-guest-${set_sha:0:8}"
base="https://github.com/voyemu/voyemu-deps/releases/download/$tag/"
# Writes the manifest from the variables that each case changes after reset.
write() {
    mkdir -p src/app
    cat > src/app/toolchain.json << EOF2
{"qnxGuestModules":{
"sa8155-qinggan09":{"initialNativeState":{},"files":{
"front.so":{$front_url"sha256":"$front_sha"},
"readstub.bin":{"url":"${base}sa8155-qinggan09-readstub.bin",$stub_sha}}},
"sa8155-passion-21480":{"initialNativeState":{},"files":{
"front.so":{"url":"$passion_url","sha256":"$c"}}}}}
EOF2
}
reset() {
    GITHUB_REF_TYPE=branch GITHUB_REF_NAME=main GITHUB_REPOSITORY=voyemu/voyemu
    front_url="\"url\":\"${base}sa8155-qinggan09-front.so\","
    front_sha=$a
    stub_sha="\"sha256\":\"$b\""
    passion_url="${base}sa8155-passion-21480-front.so"
    rm -f sums
    : > "$GITHUB_OUTPUT"
}
accept() {
    write
    bash "$selector" sums
    printf 'tag=%s\n' "$tag" > expected
    cmp expected "$GITHUB_OUTPUT"
    cmp expected-sums sums
    echo "PASS $1"
}
reject() {
    write
    if bash "$selector" sums > rejected.log 2>&1; then
        echo "FAIL accepted $1" >&2
        exit 1
    fi
    test ! -s "$GITHUB_OUTPUT"
    test ! -e sums
    echo "PASS reject $1"
}
reset
accept manual
reset
GITHUB_REF_TYPE=tag GITHUB_REF_NAME=$tag
accept tag
reset
GITHUB_REF_TYPE=tag GITHUB_REF_NAME=qnx-guest-00000000
reject tag-pin-mismatch
reset
front_sha=$c
reject sha-drift
reset
passion_url=https://example.invalid/sa8155-passion-21480-front.so
reject foreign-url
reset
GITHUB_REPOSITORY=fork/voyemu
accept independent-checkout-repository
reset
front_url=
reject missing-url
reset
stub_sha='"size":1'
reject missing-sha256
reset
front_sha=aaaaaaaa
reject malformed-sha256
reset
front_sha="$a\""
reject malformed-manifest
