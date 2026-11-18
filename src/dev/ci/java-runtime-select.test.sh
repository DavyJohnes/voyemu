#!/bin/bash
set -euo pipefail
# Run the real CI selector against isolated JSON manifests; nothing is downloaded or built.
project=$(cd "$(dirname "$0")/../../.." && pwd -P)
selector="$project/src/dev/ci/java-runtime-select.sh"
mkdir -p "$project/.local/build/jobs"
work=$(mktemp -d "$project/.local/build/jobs/java-runtime-select.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
cd "$work"
export GITHUB_REPOSITORY=voyemu/voyemu GITHUB_REF_TYPE=branch GITHUB_REF_NAME=main
export GITHUB_OUTPUT="$work/output"
a=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
b=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
c=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc
printf '%s  %s\n' "$a" java-runtime-darwin-arm64.zip "$b" java-runtime-linux-x64.zip \
    "$c" java-runtime-win32-x64.zip > expected-sums
set_sha=$(sha256sum < expected-sums)
tag="java-runtime-${set_sha:0:8}"
base="https://github.com/voyemu/voyemu-deps/releases/download/$tag/"
# Writes the manifest from the variables that each case changes after reset.
write() {
    mkdir -p src/app
    cat > src/app/toolchain.json << EOF2
{"javaRuntime":{"version":"25.0.5+8","assets":{
"darwin-arm64":{$darwin_url"sha256":"$darwin_sha"},
"linux-x64":{"url":"${base}java-runtime-linux-x64.zip",$linux_sha},
"win32-x64":{"url":"$windows_url","sha256":"$c"}}}}
EOF2
}
reset() {
    GITHUB_REF_TYPE=branch GITHUB_REF_NAME=main GITHUB_REPOSITORY=voyemu/voyemu
    darwin_url="\"url\":\"${base}java-runtime-darwin-arm64.zip\","
    darwin_sha=$a
    linux_sha="\"sha256\":\"$b\""
    windows_url="${base}java-runtime-win32-x64.zip"
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
GITHUB_REF_TYPE=tag GITHUB_REF_NAME=java-runtime-00000000
reject tag-pin-mismatch
reset
darwin_sha=$c
reject sha-drift
reset
windows_url=https://example.invalid/java-runtime-win32-x64.zip
reject foreign-url
reset
GITHUB_REPOSITORY=fork/voyemu
accept independent-checkout-repository
reset
darwin_url=
reject missing-url
reset
linux_sha='"size":1'
reject missing-sha256
reset
darwin_sha=aaaaaaaa
reject malformed-sha256
reset
darwin_sha="$a\""
reject malformed-manifest
