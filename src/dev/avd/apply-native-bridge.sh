#!/usr/bin/env bash
# Import only the profile's pinned files and property values into private copies of its ext4 images.
set -euo pipefail
umask 077
export LC_ALL=C
die() { echo "AVD_NATIVE_BRIDGE: $*" >&2; exit 1; }
[[ $# == 4 ]] || die 'Usage: apply-native-bridge.sh RECIPE SDK_ZIP BASE_FILESYSTEMS NEW_FILESYSTEMS'
authoring_spec=$(realpath -e -- "$1")
archive=$(realpath -e -- "$2")
base=$(realpath -e -- "$3")
dest=$(realpath -m -- "$4")
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
root=$(cd -- "$here/../../.." && pwd -P)
[[ ! -e "$dest" && ! -L "$dest" && "$dest" != "$base"/* ]] || die 'New destination outside the base required'
stage=$(mktemp -d "$(dirname -- "$dest")/.native-bridge.XXXXXXXX")
trap 'rm -rf -- "$stage"' EXIT
cd "$stage"
spec="$stage/native-bridge.json"
cp -- "$authoring_spec" "$spec"
checksum() { sha256sum -- "$1" | cut -d' ' -f1; }
jq -e '.schema == 1 and (.archive.sha256|test("^[a-f0-9]{64}$")) and
    (.archive.systemImageSha256|test("^[a-f0-9]{64}$")) and (.archive.member|test("^[a-zA-Z0-9_]+/system[.]img$")) and
    (.files|type == "object" and length > 0) and (.propertyFiles|type == "object") and
    ((.files|keys) - (.propertyFiles|keys) == (.files|keys))' "$spec" >/dev/null || die 'Invalid descriptor'
[[ "$(checksum "$archive")" == "$(jq -r .archive.sha256 "$spec")" ]] || die 'Donor archive SHA-256 differs'
tools=$("$here/prepare-tools.sh")
export PATH="$tools/bin:$PATH"
parts=(system system_ext product vendor)
mkdir -p output/evidence
[[ $(wc -l < "$base/filesystems.sha256") == 4 ]] || die 'Four pinned input filesystems required'
for part in "${parts[@]}"; do
    [[ -f "$base/$part.ext4" && ! -L "$base/$part.ext4" ]] || die "Missing $part"
    [[ $(grep -Ec "^[a-f0-9]{64}  ${part}[.]ext4$" "$base/filesystems.sha256") == 1 ]] || die "Wrong pin for $part"
    cp --reflink=auto --sparse=always -- "$base/$part.ext4" "output/$part.ext4"
done
(cd output && sha256sum --strict --status -c "$base/filesystems.sha256") || die 'Input filesystem SHA-256 differs'
if [[ -d "$base/evidence" ]]; then cp -a -- "$base/evidence/." output/evidence/; fi
report=output/evidence/native-bridge.tsv
[[ ! -e "$report" ]] || die 'Input already contains a bridge receipt'
printf 'target\tsha256\tmode\tuid\tgid\tcontext\n' > "$report"
unzip -p "$archive" "$(jq -r .archive.member "$spec")" > donor.img
layout_tool="$here/voyemu_avd_layout"
[[ -x "$layout_tool" ]] || layout_tool="$root/.local/build/native/voyemu_avd_layout"
"$layout_tool" donor.img "$(jq -r .archive.systemImageSha256 "$spec")" > layout.json
debug() {
    local result
    result=$(debugfs -R "$2" "$1" 2>error) || return 1
    [[ -z "$(sed '/^debugfs [0-9]/d' error)" ]] || return 1
    printf '%s\n' "$result"
}
present() {
    if stat_text=$(debug "$1" "stat $2"); then return 0; fi
    grep -q 'File not found by ext2_lookup' error || die "Cannot inspect $2"
    return 1
}
metadata() {
    [[ "$stat_text" == *"Type: $1"* && "$stat_text" =~ Mode:[[:space:]]+(0[0-7]{3})[[:space:]] ]] || die "Unexpected inode $path"
    mode=${BASH_REMATCH[1]}
    [[ "$stat_text" =~ User:[[:space:]]+([0-9]+)[[:space:]]+Group:[[:space:]]+([0-9]+) ]] || die "Missing owner $path"
    uid=${BASH_REMATCH[1]}; gid=${BASH_REMATCH[2]}
}
check_xattrs() {
    local names
    names=$(debug "$1" "ea_list $path")
    [[ -z "$(sed -nE '/^ +[^ ]+ \([0-9]+\)/ { /security\.selinux/!p; }' <<<"$names")" ]] || die "Unexpected xattr $path"
}
for part in "${parts[@]}"; do
    ctx="/etc/selinux/${part}_file_contexts"
    [[ "$part" != system ]] || ctx=/system/etc/selinux/plat_file_contexts
    if present "output/$part.ext4" "$ctx"; then
        debug "output/$part.ext4" "dump $ctx context-$part" >/dev/null
        cat "context-$part" >> contexts
        printf '\n' >> contexts
    fi
done
[[ -s context-system ]] || die 'Missing platform contexts'
write_inode() {
    local kind=$1 bits=0100 guest=$path expected_uid=$uid expected_gid=$gid expected_mode=$mode
    [[ "$kind" == file ]] || bits=040
    [[ "$part" == system ]] || guest="/$part$path"
    context=$(matchpathcon -N -n -m "$kind" -f contexts "$guest")
    [[ "$context" =~ ^u:object_r:[A-Za-z0-9_]+:s0$ ]] || die "Unknown label for $guest"
    printf '%s\0' "$context" > label
    printf 'set_inode_field %s uid %s\nset_inode_field %s gid %s\nset_inode_field %s mode %s%s\nea_set -f label %s security.selinux\n' \
        "$path" "$uid" "$path" "$gid" "$path" "$bits" "${mode#0}" "$path" >> operations
    debugfs -w -f operations "$image" >/dev/null 2>error
    [[ -z "$(sed '/^debugfs [0-9]/d' error)" ]] || die "Cannot install $guest: $(cat error)"
    present "$image" "$path" || die "Missing installed $guest"
    if [[ "$kind" == file ]]; then metadata regular; else metadata directory; fi
    [[ "$uid" == "$expected_uid" && "$gid" == "$expected_gid" && "$mode" == "$expected_mode" ]] || die "Owner or mode differs for $guest"
    debug "$image" "ea_get -f checked-label $path security.selinux" >/dev/null || die "Cannot read label for $guest"
    cmp -s label checked-label || die "Label differs for $guest"
}
parents() {
    local path=${1%/*}
    [[ -n "$path" ]] || return 0
    if present "$image" "$path"; then metadata directory; return; fi
    parents "$path"
    present "$donor" "$path" || die "Missing SDK directory $path"
    metadata directory; check_xattrs "$donor"
    printf 'mkdir %s\n' "$path" > operations
    write_inode dir
}
merge_properties() {
    debug "$image" "dump $path base.props" >/dev/null
    [[ "$(checksum base.props)" == "$(jq -er --arg p "$target" '.propertyFiles[$p].inputSha256' "$spec")" ]] || die "Wrong base properties on $target"
    debug "$donor" "dump $path donor.props" >/dev/null
    jq -er --arg p "$target" '.propertyFiles[$p].values|to_entries|sort_by(.key)[]|[.key,.value]|@tsv' "$spec" > keys
    awk -F '\t' 'NR==FNR { if ($1 !~ /^ro[.][A-Za-z0-9_.-]+$/ || NF!=2 || $1 in want) exit 1; want[$1]=$2; next }
        { i=index($0,"="); k=substr($0,1,i-1); if (k in want && substr($0,i+1)==want[k]) seen[k]++ }
        END { for(k in want) if (seen[k]!=1) exit 1 }' keys donor.props || die "Wrong SDK properties on $target"
    awk -F '\t' 'NR==FNR { names[++n]=$1; values[$1]=$2; next }
        { k=substr($0,1,index($0,"=")-1); line=$0; if(k in values){if(++seen[k]!=1)exit 1;line=k "=" values[k]} lines[++total]=line }
        END { for(j=1;j<=n;j++) if(!(names[j] in seen)){while(total && lines[total]=="")total--;lines[++total]=names[j] "=" values[names[j]]}
              for(j=1;j<=total;j++) print lines[j] }' keys base.props > payload || die "Repeated key in $target"
}
jq -r '.files|to_entries[]|[.key,.value,"add"]|@tsv' "$spec" > entries
jq -r '.propertyFiles|to_entries[]|[.key,.value.outputSha256,"merge"]|@tsv' "$spec" >> entries
while IFS=$'\t' read -r target pin action; do
    [[ "$target" =~ ^/(system|vendor)/[A-Za-z0-9_.+@/-]+$ && "$target" != *'//'* && "/${target#/}/" != *'/../'* &&
       "/${target#/}/" != *'/./'* && "$pin" =~ ^[a-f0-9]{64}$ ]] || die "Invalid entry $target"
    path=$target; part=system
    if [[ "$target" == /vendor/* ]]; then part=vendor; path=${target#/vendor}; fi
    donor="donor.img?offset=$(jq -er --arg p "$part" '.partitions[$p].offset' layout.json)"
    image="output/$part.ext4"
    parents "$path"
    : > operations
    if [[ "$action" == add ]]; then
        if present "$image" "$path"; then die "Unexpected existing file $target"; fi
        present "$donor" "$path" || die "Missing SDK file $target"
        metadata regular; check_xattrs "$donor"
        debug "$donor" "dump $path payload" >/dev/null
    else
        present "$image" "$path" || die "Missing base file $target"
        metadata regular; check_xattrs "$image"
        merge_properties
        printf 'rm %s\n' "$path" >> operations
    fi
    [[ "$(checksum payload)" == "$pin" ]] || die "Payload SHA-256 differs for $target"
    printf 'write payload %s\n' "$path" >> operations
    write_inode file
    debug "$image" "dump $path read-back" >/dev/null || die "Cannot read installed file $target"
    [[ "$(checksum read-back)" == "$pin" ]] || die "Installed bytes differ for $target"
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$target" "$pin" "$mode" "$uid" "$gid" "$context" >> "$report"
done < entries
for part in system vendor; do e2fsck -fn "output/$part.ext4" >> output/evidence/native-bridge-fsck.log 2>&1 || die "Invalid $part filesystem"; done
(cd output && sha256sum -- system.ext4 system_ext.ext4 product.ext4 vendor.ext4 > filesystems.sha256)
cmp -s "$authoring_spec" "$spec" || die 'Profile bridge inputs changed during preparation'
mv -T --no-clobber -- output "$dest"
[[ ! -d output ]] || die 'Destination was created concurrently'
printf '%s\n' "$dest"
