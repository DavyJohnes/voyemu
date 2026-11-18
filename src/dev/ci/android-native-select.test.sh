#!/bin/bash
set -euo pipefail
# Run the real CI selector against isolated JSON manifests; nothing is downloaded or built.
project=$(cd "$(dirname "$0")/../../.." && pwd -P)
selector="$project/src/dev/ci/android-native-select.sh"
mkdir -p "$project/.local/build/jobs"
work=$(mktemp -d "$project/.local/build/jobs/android-native-select.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
cd "$work"
export GITHUB_REPOSITORY=voyemu/voyemu GITHUB_REF_TYPE=branch GITHUB_REF_NAME=main
export GITHUB_OUTPUT="$work/output"
a=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
b=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
c=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc
printf '%s  %s\n' "$a" p9-qg-runtime-libqg_runtime.so "$b" p9-early-inputs-voyemu-early-inputs \
    "$c" p11-can-simulator-voyemu-can-model > expected-sums
set_sha=$(sha256sum < expected-sums)
tag="android-native-${set_sha:0:8}"
base="https://github.com/voyemu/voyemu/releases/download/$tag/"
# Writes the manifest from the variables that each case changes after reset.
write() {
    mkdir -p src/app
    cat > src/app/toolchain.json << EOF2
{"androidNativeComponents":{
"p9/qg-runtime":{$front_url"sha256":"$front_sha"},
"p9/early-inputs":{"url":"${base}p9-early-inputs-voyemu-early-inputs",$stub_sha},
"p11/can-simulator":{"url":"$passion_url","sha256":"$c"}}}
EOF2
}
reset() {
    GITHUB_REF_TYPE=branch GITHUB_REF_NAME=main GITHUB_REPOSITORY=voyemu/voyemu
    front_url="\"url\":\"${base}p9-qg-runtime-libqg_runtime.so\","
    front_sha=$a
    stub_sha="\"sha256\":\"$b\""
    passion_url="${base}p11-can-simulator-voyemu-can-model"
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
GITHUB_REF_TYPE=tag GITHUB_REF_NAME=android-native-00000000
reject tag-pin-mismatch
reset
front_sha=$c
reject sha-drift
reset
passion_url=https://example.invalid/p11-can-simulator-voyemu-can-model
reject foreign-url
reset
GITHUB_REPOSITORY=fork/voyemu
reject foreign-repository
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
