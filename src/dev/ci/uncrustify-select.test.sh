#!/bin/bash
set -euo pipefail
# Run the real CI selector against isolated JSON manifests; nothing is downloaded or built.
project=$(cd "$(dirname "$0")/../../.." && pwd -P)
selector="$project/src/dev/ci/uncrustify-select.sh"
mkdir -p "$project/.local/build/jobs"
work=$(mktemp -d "$project/.local/build/jobs/uncrustify-select.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
cd "$work"
export GITHUB_REPOSITORY=voyemu/voyemu GITHUB_REF_TYPE=branch GITHUB_REF_NAME=main
export GITHUB_OUTPUT="$work/output"
printf '%s  %s\n' aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa uncrustify-0.83.0-darwin-arm64 \
    bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb uncrustify-0.83.0-linux-x64 \
    cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc uncrustify-0.83.0-win32-x64.exe > expected-sums
set_sha=$(sha256sum < expected-sums)
tag="uncrustify-0.83.0-${set_sha:0:8}"
base="https://github.com/voyemu/voyemu/releases/download/$tag/"
# Writes the manifest from the variables that each case changes after reset.
write() {
    mkdir -p src/app
    cat > src/app/toolchain.json << EOF
{"optionalTools":{"uncrustify":{"version":"$version","binaries":{
"darwin-arm64":{$darwin_url"sha256":"$darwin_sha"},
"linux-x64":{"url":"${base}uncrustify-0.83.0-linux-x64",$linux_sha},
"win32-x64":{"url":"$windows_url","sha256":"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"}}}}}
EOF
}
reset() {
    GITHUB_REF_TYPE=branch GITHUB_REF_NAME=main GITHUB_REPOSITORY=voyemu/voyemu
    version=0.83.0
    darwin_url="\"url\":\"${base}uncrustify-0.83.0-darwin-arm64\","
    darwin_sha=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
    linux_sha='"sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"'
    windows_url="${base}uncrustify-0.83.0-win32-x64.exe"
    rm -f sums
    : > "$GITHUB_OUTPUT"
}
accept() {
    write
    bash "$selector" sums
    printf 'version=0.83.0\ntag=%s\n' "$tag" > expected
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
GITHUB_REF_TYPE=tag GITHUB_REF_NAME=uncrustify-0.83.0-00000000
reject tag-pin-mismatch
reset
darwin_sha=dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd
reject sha-drift
reset
windows_url=https://example.invalid/uncrustify.exe
reject foreign-url
reset
GITHUB_REPOSITORY=fork/voyemu
reject foreign-repository
reset
version=0.84.0
reject version-drift
reset
version=latest
reject malformed-version
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
version='0.83.0"'
reject malformed-manifest
