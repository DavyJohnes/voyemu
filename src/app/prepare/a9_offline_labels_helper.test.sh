#!/bin/bash
set -euo pipefail
# Arguments: helper, mke2fs, debugfs, e2fsck, fresh owned work directory.
helper=$1; mkfs=$2; debugfs=$3; fsck=$4; work=$5
project="$(cd "$(dirname "$0")/../../.." && pwd)"
jdk_bin=${JAVA_HOME:+"$JAVA_HOME/bin/"}
mkdir "$work"
mkdir "$work/classes"
"${jdk_bin}javac" --release 25 -d "$work/classes" "$project/src/prepare/test/java/com/voyemu/prepare/A9OfflineLabelsFixture.java"
gpt_fixture() { "${jdk_bin}java" -cp "$work/classes" com.voyemu.prepare.A9OfflineLabelsFixture "$@"; }
mkdir -p "$work/root/system/bin" "$work/root/system/etc/voyemu"
printf 'first executable\n' > "$work/root/system/bin/voyemu-early-inputs"
printf 'second executable\n' > "$work/root/system/bin/voyemu-can-model"
chmod 755 "$work/root/system/bin/"voyemu-*
printf 'fixture\n' > "$work/root/system/etc/voyemu/can-model.fixture"
printf 'database\n' > "$work/root/system/etc/voyemu/qg-settings-guest.db"
printf 'nvram\n' > "$work/root/system/etc/voyemu/hybrid-nvram.img"
printf 'untouched neighbor\n' > "$work/root/system/etc/voyemu/neighbor"
chmod 644 "$work/root/system/etc/voyemu/can-model.fixture"
chmod 444 "$work/root/system/etc/voyemu/qg-settings-guest.db" "$work/root/system/etc/voyemu/hybrid-nvram.img"
truncate -s 32m "$work/part"
"$mkfs" -q -t ext4 -d "$work/root" -F "$work/part"
printf 'u:object_r:system_file:s0\0' > "$work/old"
printf 'u:object_r:unrelated:s0\0' > "$work/wrong"
printf 'other attribute survives\0' > "$work/other"
for name in bin/voyemu-early-inputs bin/voyemu-can-model etc/voyemu/can-model.fixture etc/voyemu/qg-settings-guest.db etc/voyemu/hybrid-nvram.img; do
    "$debugfs" -w -R "set_inode_field /system/$name uid 0" "$work/part" >/dev/null 2>&1
    "$debugfs" -w -R "set_inode_field /system/$name gid 0" "$work/part" >/dev/null 2>&1
    "$debugfs" -w -R "ea_set -f $work/old /system/$name security.selinux" "$work/part" >/dev/null 2>&1
    "$debugfs" -w -R "ea_set -f $work/other /system/$name user.fixture" "$work/part" >/dev/null 2>&1
done
"$debugfs" -w -R 'ln /system/etc/voyemu/neighbor /system/etc/voyemu/neighbor-hardlink' "$work/part" >/dev/null 2>&1
"$debugfs" -w -R 'set_inode_field /system/etc/voyemu/neighbor links_count 2' "$work/part" >/dev/null 2>&1
"$debugfs" -w -R "ea_set -f $work/other /system/etc/voyemu/neighbor user.fixture" "$work/part" >/dev/null 2>&1
snapshot() {
    local part=$1 result=$2 path
    : > "$result"
    for path in / /system /system/bin /system/etc /system/etc/voyemu /system/etc/voyemu/neighbor /system/etc/voyemu/neighbor-hardlink; do
        "$debugfs" -R "stat $path" "$part" 2>/dev/null >> "$result"
    done
    "$debugfs" -R 'ea_list /system/etc/voyemu/neighbor' "$part" 2>/dev/null >> "$result"
    "$debugfs" -R 'ea_get /system/etc/voyemu/neighbor user.fixture' "$part" 2>/dev/null >> "$result"
    "$debugfs" -R "dump /system/etc/voyemu/neighbor $work/neighbor-dump" "$part" >/dev/null 2>&1
    hash "$work/neighbor-dump" >> "$result"
    rm "$work/neighbor-dump"
}
wrap() {
    gpt_fixture wrap "$work/part" "$1"
}
hash() { shasum -a 256 "$1" | cut -d ' ' -f 1; }
early=$(hash "$work/root/system/bin/voyemu-early-inputs")
model=$(hash "$work/root/system/bin/voyemu-can-model")
fixture=$(hash "$work/root/system/etc/voyemu/can-model.fixture")
database=$(hash "$work/root/system/etc/voyemu/qg-settings-guest.db")
nvram=$(hash "$work/root/system/etc/voyemu/hybrid-nvram.img")
zeros=0000000000000000000000000000000000000000000000000000000000000000
reject_unchanged() { local before; before=$(hash "$work/image"); if "$helper" "$work/image" "$1" "$2" "$3" "$fixture" "$database" "$nvram" >"$work/rejected.log" 2>&1; then echo 'unexpected success' >&2; exit 1; fi; test "$(hash "$work/image")" = "$before"; }
snapshot "$work/part" "$work/neighbor-before"
wrap "$work/image"
"$debugfs" -R 'ea_get /system/bin/voyemu-early-inputs security.selinux' "$work/part" 2>/dev/null | grep -F 'u:object_r:system_file:s0' >/dev/null
if "$debugfs" -R 'ea_get /system/bin/voyemu-early-inputs security.selinux' "$work/part" 2>/dev/null | grep -F 'u:object_r:can_early_inputs_exec:s0' >/dev/null; then exit 1; fi
old=$(hash "$work/image")
reject_unchanged "$zeros" "$early" "$model"
reject_unchanged "$old" "$zeros" "$model"
gpt_fixture corrupt-backup "$work/image"
reject_unchanged "$(hash "$work/image")" "$early" "$model"
wrap "$work/recreated"
mv "$work/recreated" "$work/image"
old=$(hash "$work/image")
ln -s "$work/image" "$work/link"
if "$helper" "$work/link" "$old" "$early" "$model" "$fixture" "$database" "$nvram" >"$work/rejected.log" 2>&1; then exit 1; fi
"$helper" "$work/image" "$old" "$early" "$model" "$fixture" "$database" "$nvram"
dd if="$work/image" of="$work/result-part" bs=1048576 skip=1 count=32 2>/dev/null
"$fsck" -fn "$work/result-part" >"$work/fsck.log" 2>&1
snapshot "$work/result-part" "$work/neighbor-after"
cmp "$work/neighbor-before" "$work/neighbor-after"
"$debugfs" -R 'ea_get /system/bin/voyemu-early-inputs security.selinux' "$work/result-part" 2>/dev/null | grep -F 'u:object_r:can_early_inputs_exec:s0' >/dev/null
"$debugfs" -R 'ea_get /system/bin/voyemu-can-model security.selinux' "$work/result-part" 2>/dev/null | grep -F 'u:object_r:can_simulator_exec:s0' >/dev/null
"$debugfs" -R 'ea_get /system/etc/voyemu/can-model.fixture security.selinux' "$work/result-part" 2>/dev/null | grep -F 'u:object_r:can_simulator_fixture:s0' >/dev/null
"$debugfs" -R 'ea_get /system/etc/voyemu/qg-settings-guest.db security.selinux' "$work/result-part" 2>/dev/null | grep -F 'u:object_r:can_early_input_file:s0' >/dev/null
"$debugfs" -R 'ea_get /system/etc/voyemu/hybrid-nvram.img security.selinux' "$work/result-part" 2>/dev/null | grep -F 'u:object_r:can_early_input_file:s0' >/dev/null
"$debugfs" -R 'ea_get /system/bin/voyemu-can-model user.fixture' "$work/result-part" 2>/dev/null | grep -F 'other attribute survives' >/dev/null
current=$(hash "$work/image")
"$helper" "$work/image" "$current" "$early" "$model" "$fixture" "$database" "$nvram"
"$debugfs" -w -R 'set_super_value state 0' "$work/part" >/dev/null 2>&1
wrap "$work/image-dirty"
mv "$work/image-dirty" "$work/image"
reject_unchanged "$(hash "$work/image")" "$early" "$model"
grep -F 'ext4 requires clean unmounted filesystem' "$work/rejected.log" >/dev/null
dirty_sha=$(hash "$work/image")
"$helper" "$work/image" "$dirty_sha" "$early" "$model" "$fixture" "$database" "$nvram" --check-before-fsck > "$work/dirty-inspection.txt"
test "$(hash "$work/image")" = "$dirty_sha"
grep -E '^offset=1048576 bytes=[0-9]+ outside_sha256=[0-9a-f]{64}$' "$work/dirty-inspection.txt" >/dev/null
if "$helper" "$work/image" "$zeros" "$early" "$model" "$fixture" "$database" "$nvram" --check-before-fsck > "$work/rejected.log" 2>&1; then exit 1; fi
test "$(hash "$work/image")" = "$dirty_sha"
"$debugfs" -w -R 'set_super_value state 1' "$work/part" >/dev/null 2>&1
incompat=$(gpt_fixture ext4-incompat "$work/part")
"$debugfs" -w -R "set_super_value feature_incompat $((incompat | 4))" "$work/part" >/dev/null 2>&1
wrap "$work/image-recovery"
mv "$work/image-recovery" "$work/image"
reject_unchanged "$(hash "$work/image")" "$early" "$model"
"$debugfs" -w -R "set_super_value feature_incompat $incompat" "$work/part" >/dev/null 2>&1
"$debugfs" -w -R 'set_inode_field /system/etc/voyemu/hybrid-nvram.img mode 0100644' "$work/part" >/dev/null 2>&1
wrap "$work/image-bad-mode"
mv "$work/image-bad-mode" "$work/image"
reject_unchanged "$(hash "$work/image")" "$early" "$model"
"$debugfs" -w -R 'set_inode_field /system/etc/voyemu/hybrid-nvram.img mode 0100444' "$work/part" >/dev/null 2>&1
"$debugfs" -w -R "ea_set -f $work/wrong /system/bin/voyemu-can-model security.selinux" "$work/part" >/dev/null 2>&1
wrap "$work/image-bad-context"
mv "$work/image-bad-context" "$work/image"
reject_unchanged "$(hash "$work/image")" "$early" "$model"
printf X | dd of="$work/image" bs=1 seek=512 conv=notrunc 2>/dev/null
reject_unchanged "$(hash "$work/image")" "$early" "$model"
"$debugfs" -w -R 'unlink /system/bin/voyemu-can-model' "$work/part" >/dev/null 2>&1
wrap "$work/image-new"
mv "$work/image-new" "$work/image"
reject_unchanged "$(hash "$work/image")" "$early" "$model"
printf 'offline label GPT fixture passed\n'
