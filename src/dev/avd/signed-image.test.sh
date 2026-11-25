#!/usr/bin/env bash
# Hardware-independent checks against an actual release. Never write into the tested release.
set -euo pipefail
umask 077
export LC_ALL=C PYTHONDONTWRITEBYTECODE=1
[[ $# == 3 ]] || { echo "Usage: $0 RELEASE PUBLIC_PEM EXPECTED_SPKI_SHA256" >&2; exit 2; }
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
release=$(realpath -- "$1")
pub=$(realpath -- "$2")
pin=$3
"$here/verify-image.sh" "$release" "$pub" "$pin"
work=$(mktemp -d "$(dirname -- "$release")/.avd-integrity-test.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
foreign_pin="0${pin:1}"
[[ "$foreign_pin" != "$pin" ]] || foreign_pin="1${pin:1}"
if "$here/verify-image.sh" "$release" "$pub" "$foreign_pin" > "$work/negative-key" 2>&1; then
    echo 'Unprovisioned key identity was accepted' >&2; exit 1
fi
grep -Fq 'Wrong pinned public key' "$work/negative-key"
cp -- "$release/manifest.sha256" "$work/manifest.sha256"
printf '\nchanged\n' >> "$work/manifest.sha256"
if openssl dgst -sha256 -verify "$pub" -signature "$release/manifest.sig" "$work/manifest.sha256" > "$work/negative-manifest" 2>&1; then
    echo 'Modified manifest was accepted' >&2; exit 1
fi
grep -qi 'verification failure' "$work/negative-manifest"
cp --reflink=auto --sparse=always -- "$release/partitions/system.img" "$work/system.img"
# Flip a bit in the ext4 data that the signed root descriptor covers. Keep its AVB footer.
byte=$(od -An -tu1 -N1 -j1048576 "$work/system.img")
printf -v replacement '%03o' "$((byte ^ 1))"
printf '%b' "\\$replacement" | dd of="$work/system.img" bs=1 seek=1048576 conv=notrunc status=none
tools=$("$here/prepare-tools.sh" --verify-only)
if "$tools/bin/avbtool" verify_image --image "$work/system.img" --key "$pub" > "$work/negative-system" 2>&1; then
    echo 'Changed filesystem was accepted' >&2; exit 1
fi
grep -Fq 'does not match descriptor' "$work/negative-system"
echo 'Valid release accepted; wrong trust pin, changed signed manifest and changed ext4 rejected.'
