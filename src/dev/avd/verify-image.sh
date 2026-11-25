#!/usr/bin/env bash
set -euo pipefail
umask 077
export LC_ALL=C PYTHONDONTWRITEBYTECODE=1
if [[ $# != 3 ]]; then
    echo "Usage: $0 RELEASE TRUSTED_PUBLIC_PEM TRUSTED_SPKI_SHA256" >&2
    exit 2
fi
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
release=$(realpath -- "$1")
pub=$(realpath -- "$2")
expected=$3
[[ "$expected" =~ ^[[:xdigit:]]{64}$ && "$pub" != "$release/"* ]] || exit 2
actual=$(openssl pkey -pubin -in "$pub" -pubout -outform DER | sha256sum)
[[ "${actual%% *}" == "$expected" ]] || { echo 'Wrong pinned public key' >&2; exit 1; }
cd "$release"
for input in kernel-ranchu ramdisk.img system.img vendor.img userdata.img encryptionkey.img VerifiedBootParams.textproto; do
    [[ -s "sdk/$input" && ! -L "sdk/$input" ]] || exit 1
done
[[ -d sdk/data && -n "$(find sdk/data -type f -print -quit)" ]] || exit 1
openssl dgst -sha256 -verify "$pub" -signature manifest.sig manifest.sha256
[[ -z "$(find sdk partitions ! -type f ! -type d -print -quit)" ]] || exit 1
temp=$(mktemp -d)
trap 'rm -rf -- "$temp"' EXIT
while IFS= read -r line; do
    [[ "$line" =~ ^[a-f0-9]{64}[[:space:]]\ ([A-Za-z0-9_./+@-]+)$ ]] || exit 1
    path=${BASH_REMATCH[1]}
    [[ "$path" != /* && "/$path/" != *'/../'* && "/$path/" != *'/./'* ]] || exit 1
    printf '%s\n' "$path" >> "$temp/list"
done < manifest.sha256
{ find sdk partitions -type f -print; printf '%s\n' release.properties; } | sort > "$temp/expected"
sort "$temp/list" | cmp - "$temp/expected"
sha256sum --strict --check --status manifest.sha256
tools=$("$here/prepare-tools.sh" --verify-only)
export PATH="$tools/bin:$PATH" LD_LIBRARY_PATH="$tools/lib64"
avbtool extract_public_key --key "$pub" --output "$temp/key.avbpubkey"
avbtool verify_image --image partitions/vbmeta.img --key "$pub" \
    --expected_chain_partition "system:1:$temp/key.avbpubkey" --follow_chain_partitions
avbtool verify_image --image partitions/system.img --key "$pub"
digest=$(avbtool calculate_vbmeta_digest --image partitions/vbmeta.img --hash_algorithm sha256)
top=$(avbtool info_image --image partitions/vbmeta.img)
system=$(avbtool info_image --image partitions/system.img)
[[ "$(awk '/^Descriptors:/ {exit} $1=="Flags:" {print $2}' <<< "$top")" == 0 &&
   "$(awk '/^Descriptors:/ {exit} $1=="Flags:" {print $2}' <<< "$system")" == 0 ]] || {
    echo 'Expected AVB integrity checks on the signed root and system' >&2
    exit 1
}
child=$(awk '$1=="VBMeta" && $2=="size:" {print $3}' <<< "$system")
system_root=$(awk '$1=="Root" && $2=="Digest:" {print $3}' <<< "$system")
[[ "$child" =~ ^[0-9]+$ ]]
[[ "$system_root" =~ ^[a-f0-9]{64}$ ]]
bytes=$(( $(stat -c %s partitions/vbmeta.img) + child ))
printf 'major_version: 2\nparam: "androidboot.vbmeta.size=%s"\nparam: "androidboot.vbmeta.hash_alg=sha256"\nparam: "androidboot.vbmeta.digest=%s"\n' \
    "$bytes" "$digest" | cmp - sdk/VerifiedBootParams.textproto
printf 'Verified system root: %s\n' "$system_root"
