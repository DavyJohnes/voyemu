#!/bin/bash
set -euo pipefail
# Run the real CI selector against isolated JSON manifests; nothing is downloaded or built.
project=$(cd "$(dirname "$0")/../../.." && pwd -P)
selector="$project/src/dev/ci/e2fs-helpers-select.sh"
mkdir -p "$project/.local/build/jobs"
work=$(mktemp -d "$project/.local/build/jobs/e2fs-helpers-select.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
cd "$work"
export GITHUB_REPOSITORY=voyemu/voyemu GITHUB_REF_TYPE=branch GITHUB_REF_NAME=main
export GITHUB_OUTPUT="$work/output"
a=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
b=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
c=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc
d=dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd
e=eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee
f=ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
printf '%s  %s\n' "$a" a9-offline-labels-darwin-arm64 "$b" api30-root-layout-darwin-arm64 \
    "$c" a9-offline-labels-linux-x64 "$d" api30-root-layout-linux-x64 \
    "$e" a9-offline-labels-win32-x64 "$f" api30-root-layout-win32-x64 > expected-sums
set_sha=$(sha256sum < expected-sums)
tag="e2fs-helpers-${set_sha:0:8}"
base="https://github.com/voyemu/voyemu/releases/download/$tag/"
# Writes the manifest from the variables that each case changes after reset.
write() {
    mkdir -p src/app
    cat > src/app/toolchain.json << EOF
{"e2fsprogs":{"version":"1.47.4","helpers":{
"darwin-arm64":{"a9-offline-labels":{$darwin_url"sha256":"$darwin_sha"},
"api30-root-layout":{"url":"${base}api30-root-layout-darwin-arm64","sha256":"$b"}},
"linux-x64":{"a9-offline-labels":{"url":"${base}a9-offline-labels-linux-x64",$linux_sha},
"api30-root-layout":{"url":"$linux_url","sha256":"$d"}},
"win32-x64":{"a9-offline-labels":{"url":"${base}a9-offline-labels-win32-x64","sha256":"$e"},
"api30-root-layout":{"url":"${base}api30-root-layout-win32-x64","sha256":"$f"}}}}}
EOF
}
reset() {
    GITHUB_REF_TYPE=branch GITHUB_REF_NAME=main GITHUB_REPOSITORY=voyemu/voyemu
    darwin_url="\"url\":\"${base}a9-offline-labels-darwin-arm64\","
    darwin_sha=$a
    linux_sha="\"sha256\":\"$c\""
    linux_url="${base}api30-root-layout-linux-x64"
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
GITHUB_REF_TYPE=tag GITHUB_REF_NAME=e2fs-helpers-00000000
reject tag-pin-mismatch
reset
darwin_sha=$d
reject sha-drift
reset
linux_url=https://example.invalid/api30-root-layout
reject foreign-url
reset
GITHUB_REPOSITORY=fork/voyemu
reject foreign-repository
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
