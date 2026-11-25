#!/usr/bin/env bash
# Fast negative inputs and a positive readback for the private filesystem producer.
set -euo pipefail
umask 077
export LC_ALL=C
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
[[ $# == 2 ]] || { echo 'Usage: native-bridge.test.sh RECIPE PREPARED_FILESYSTEMS' >&2; exit 2; }
spec=$(realpath -- "$1")
made=$(realpath -- "$2")
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
cd "$work"
mkdir base
printf 'unexpected donor archive' > wrong.zip
if "$here/apply-native-bridge.sh" "$spec" wrong.zip base result > log 2>&1; then exit 1; fi
grep -q 'Donor archive SHA-256 differs' log
[[ ! -e result ]]
mkdir retained
printf 'existing data\n' > retained/marker
if "$here/apply-native-bridge.sh" "$spec" wrong.zip base retained > log 2>&1; then exit 1; fi
grep -q 'New destination' log && grep -qx 'existing data' retained/marker
jq -r '(.files|to_entries[]|[.key,.value]),(.propertyFiles|to_entries[]|[.key,.value.outputSha256])|@tsv' "$spec" | sort > expected
tail -n +2 "$made/evidence/native-bridge.tsv" | cut -f1,2 | sort > actual
cmp expected actual
# Verify the declared bytes in the final ext4 instead of trusting the installation log.
while IFS=$'\t' read -r guest sha; do
    image="$made/system.ext4"; path=$guest
    if [[ "$guest" == /vendor/* ]]; then image="$made/vendor.ext4"; path=${guest#/vendor}; fi
    [[ "$path" =~ ^/[A-Za-z0-9_.+@/-]+$ ]] || exit 1
    debugfs -R "dump $path readback" "$image" >/dev/null 2>error
    [[ -z "$(sed '/^debugfs [0-9]/d' error)" && "$(sha256sum readback | cut -d' ' -f1)" == "$sha" ]]
done < expected
echo 'SDK mismatch and occupied destination rejected; all configured file and property bytes verified.'
