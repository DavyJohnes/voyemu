#!/bin/bash
set -euo pipefail
# Run the real CI selector against isolated JSON manifests; nothing is downloaded or built.
project=$(cd "$(dirname "$0")/../../.." && pwd -P)
selector="$project/src/dev/ci/android-java-select.sh"
mkdir -p "$project/.local/build/jobs"
work=$(mktemp -d "$project/.local/build/jobs/android-java-select.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
cd "$work"
export GITHUB_REPOSITORY=voyemu/voyemu GITHUB_REF_TYPE=branch GITHUB_REF_NAME=main
export GITHUB_OUTPUT="$work/output"
a=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
b=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
printf '%s  %s\n' "$a" android-java.zip > expected-sums
set_sha=$(sha256sum < expected-sums)
tag="android-java-${set_sha:0:8}"
base="https://github.com/voyemu/voyemu/releases/download/$tag/"
# Writes the manifest from the variables that each case changes after reset.
write() {
    mkdir -p src/app
    printf '{"androidJava":{%s%s}}\n' "$url" "$sha" > src/app/toolchain.json
}
reset() {
    GITHUB_REF_TYPE=branch GITHUB_REF_NAME=main GITHUB_REPOSITORY=voyemu/voyemu
    url="\"url\":\"${base}android-java.zip\","
    sha="\"sha256\":\"$a\""
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
GITHUB_REF_TYPE=tag GITHUB_REF_NAME=android-java-00000000
reject tag-pin-mismatch
reset
sha="\"sha256\":\"$b\""
reject sha-drift
reset
url='"url":"https://example.invalid/android-java.zip",'
reject foreign-url
reset
GITHUB_REPOSITORY=fork/voyemu
reject foreign-repository
reset
url=
reject missing-url
reset
sha='"size":1'
reject missing-sha256
reset
sha='"sha256":"aaaaaaaa"'
reject malformed-sha256
reset
sha="\"sha256\":\"$a\"\""
reject malformed-manifest
