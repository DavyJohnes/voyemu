#!/usr/bin/env bash
# Update the build display ID in a private ext4 image before AVB signing.
set -euo pipefail
umask 077
export LC_ALL=C
die() { echo "AVD_BUILD_DISPLAY_ID: $*" >&2; exit 1; }
[[ $# == 2 ]] || die "Usage: $0 SYSTEM_EXT4 BUILD_DISPLAY_ID"
value=$2
[[ "$value" =~ ^[A-Za-z0-9][A-Za-z0-9_.+-]{0,90}$ ]] || die 'Invalid build display ID'
[[ -f "$1" && ! -L "$1" ]] || die 'Ordinary system image required'
image=$(realpath -e -- "$1")
work=$(mktemp -d "$(dirname -- "$image")/.build-display-id.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
cd "$work"
path=/system/build.prop

debug() {
    debugfs "$@" "$image" 2>error || die "$(cat error)"
    [[ -z "$(sed '/^debugfs [0-9]/d' error)" ]] || die "$(cat error)"
}
stat_fields() {
    local info mode
    info=$(debug -R "stat $path") || return 1
    [[ "$info" == *'Type: regular'* && "$info" =~ Links:[[:space:]]+1[[:space:]] &&
       "$info" =~ Mode:[[:space:]]+0([0-7]{3})[[:space:]] ]] || die "Expected regular, unlinked $path"
    mode=${BASH_REMATCH[1]}
    [[ "$info" =~ User:[[:space:]]+([0-9]+)[[:space:]]+Group:[[:space:]]+([0-9]+) ]] || die "Missing ownership for $path"
    printf '%s %s %s\n' "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "$mode"
}
metadata=$(stat_fields)
read -r uid gid mode <<<"$metadata"
attrs=$(debug -R "ea_list $path")
[[ -z "$(sed -nE '/^ +[^ ]+ \([0-9]+\)/ { /security\.selinux/!p; }' <<<"$attrs")" ]] || die "Unexpected attributes for $path"
debug -R "ea_get -f label $path security.selinux" >/dev/null
[[ -s label ]] || die "Missing SELinux label for $path"
debug -R "dump $path original" >/dev/null
awk -v value="$value" '
    /^ro[.]build[.]display[.]id=/ { $0 = "ro.build.display.id=" value; count++ }
    { print }
    END { if (count != 1) exit 1 }' original > updated || die "Expected one ro.build.display.id in $path"
{
    printf 'rm %s\nwrite updated %s\n' "$path" "$path"
    printf 'set_inode_field %s uid %s\nset_inode_field %s gid %s\nset_inode_field %s mode 0100%s\n' \
        "$path" "$uid" "$path" "$gid" "$path" "$mode"
    printf 'ea_set -f label %s security.selinux\n' "$path"
} > operations
debug -w -f operations >/dev/null
debug -R "dump $path read-back" >/dev/null
debug -R "ea_get -f label-back $path security.selinux" >/dev/null
installed_metadata=$(stat_fields)
if ! cmp -s updated read-back || ! cmp -s label label-back || [[ "$installed_metadata" != "$metadata" ]]; then
    die "Installed properties or metadata differ for $path"
fi
