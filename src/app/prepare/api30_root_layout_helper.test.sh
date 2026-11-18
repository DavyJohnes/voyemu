#!/bin/bash
set -euo pipefail
[[ $# == 4 ]] || exit 2
helper="$1"; mke2fs="$2"; debugfs="$3"; work="$4"
mkdir -p "$work"
image="$work/root-layout-test.img"
slice="$work/root-layout-test.ext4"
truncate -s 32m "$slice"
MKE2FS_CONFIG="$(dirname "$mke2fs")/mke2fs.conf" "$mke2fs" -q -F -t ext4 "$slice"
truncate -s 33562624 "$image"
dd if="$slice" of="$image" bs=4096 seek=1 conv=notrunc status=none
printf prefix | dd of="$image" conv=notrunc status=none
printf suffix | dd of="$image" bs=1 seek=33558528 conv=notrunc status=none
dd if="$image" of="$work/prefix.before" bs=4096 count=1 status=none
dd if="$image" of="$work/suffix.before" bs=4096 skip=8193 count=1 status=none
hash() { shasum -a 256 "$1" | awk '{print $1}'; }
original="$(hash "$image")"
reject() { if "$helper" "$@"; then echo 'unexpected success' >&2; exit 1; fi; [[ "$(hash "$image")" == "$original" ]]; }
reject "$image" 0000000000000000000000000000000000000000000000000000000000000000 4096 33554432
reject "$image" "$original" 8192 33550336
ln "$image" "$work/alias.img"
reject "$image" "$original" 4096 33554432
rm "$work/alias.img"
ln -s "$image" "$work/link.img"
reject "$work/link.img" "$original" 4096 33554432
rm "$work/link.img"
"$helper" "$image" "$original" 4096 33554432
dd if="$image" of="$slice" bs=4096 skip=1 count=8192 status=none
dd if="$image" of="$work/prefix.after" bs=4096 count=1 status=none
dd if="$image" of="$work/suffix.after" bs=4096 skip=8193 count=1 status=none
cmp "$work/prefix.before" "$work/prefix.after"
cmp "$work/suffix.before" "$work/suffix.after"
for path in /private /private/configs /private/configs/token; do
 stat="$($debugfs -R "stat $path" "$slice" 2>/dev/null)"
 [[ "$stat" == *'Mode:  0755'* && "$stat" == *'User:     0   Group:  1000'* && "$stat" == *'u:object_r:can_early_token_dir:s0'* ]]
done
ls="$($debugfs -R 'ls -l /private/configs/token' "$slice" 2>/dev/null)"
[[ "$ls" != *accountInfo* ]]
patched="$(hash "$image")"
if "$helper" "$image" "$patched" 4096 33554432; then echo 'repeat unexpectedly succeeded' >&2; exit 1; fi
[[ "$(hash "$image")" == "$patched" ]]
echo 'API30 root layout helper tests passed'
