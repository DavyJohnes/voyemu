#!/usr/bin/env bash
# Assemble four private logical filesystems from a checked preparation epoch.
set -euo pipefail
umask 077
export LC_ALL=C
if [[ $# != 4 && $# != 5 ]]; then
    echo "Usage: $0 PROFILE ARTIFACT_RECEIPT SHA256 NEW_FILESYSTEM_DIRECTORY [x86]" >&2
    exit 2
fi
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
root=$(cd -- "$here/../../.." && pwd -P)
profile=$1
[[ "$profile" =~ ^[a-zA-Z0-9._-]+$ && "$profile" != . && "$profile" != .. ]] || exit 2
receipt=$(realpath -- "$2")
pin=$3
dest=$(realpath -m -- "$4")
die() { echo "AVD_FILESYSTEM: $*" >&2; exit 1; }
[[ "$pin" =~ ^[a-f0-9]{64}$ && ! -e "$dest" && ! -L "$dest" ]] || die 'Fresh output and receipt pin required'
profile_target=${5:-}
[[ -z "$profile_target" || "$profile_target" == x86 ]] || die 'Unsupported build target'
recipe="$root/profiles/$profile"
args=("$root" "$profile" "$receipt" "$pin")
if [[ -n "$profile_target" ]]; then
    recipe="$recipe/targets/$profile_target"
    args+=("$profile_target")
fi
recipe="$recipe/filesystems.json"
[[ -f "$recipe" && ! -L "$recipe" ]] || die 'Profile has no offline filesystem recipe'
tools=$("$here/prepare-tools.sh")
export PATH="$tools/bin:$PATH"
for tool in debugfs dumpe2fs e2fsck resize2fs matchpathcon readelf unzip jq; do
    command -v "$tool" >/dev/null || die "Missing Linux tool $tool"
done
stage=$(mktemp -d "$(dirname -- "$dest")/.oem-filesystems.XXXXXXXX")
trap 'rm -rf -- "$stage"' EXIT
cd "$stage"
mkdir output evidence
checksum() { sha256sum -- "$1" | cut -d' ' -f1; }
inputs="$here/voyemu_avd_filesystem_inputs"
layout="$here/voyemu_avd_layout"
[[ -x "$inputs" ]] || inputs="$root/.local/build/native/voyemu_avd_filesystem_inputs"
[[ -x "$layout" ]] || layout="$root/.local/build/native/voyemu_avd_layout"
[[ -x "$inputs" && -x "$layout" ]] || die 'Build voyemu_avd_filesystem_inputs and voyemu_avd_layout first'
"$inputs" "${args[@]}" > plan.json
base=$(jq -er .base plan.json)
sdk=$(jq -er .sdk plan.json)
sdk_abi=$(jq -er '.android.package|split(";")|last' plan.json)
[[ "$sdk_abi" == x86_64 && "$(jq -er .guestAbi "$recipe")" == "$sdk_abi" ]] || die 'Unsupported guest ABI'
jq -e '.schema == 1' "$recipe" >/dev/null
aapt2=$(find "$sdk/build-tools" -mindepth 2 -maxdepth 2 -type f -name aapt2)
[[ -x "$aapt2" ]] || die 'Single prepared SDK build-tools required'
"$layout" "$base/system.img" "$(jq -er '.android.clean_base.images["system.img"]' plan.json)" > layout.json
[[ "$(jq -er .metadata_sha256 layout.json)" == "$(jq -er .android.clean_base.logical_partitions.metadata_sha256 plan.json)" ]] || die 'Source LP metadata differs'
parts=(system system_ext product vendor)
checkfs() { e2fsck -fn "$1" >> evidence/fsck.log 2>&1 || die "Filesystem check failed for $1"; }
for part in "${parts[@]}"; do
    image="output/$part.ext4"
    off=$(jq -er --arg p "$part" '.partitions[$p].offset' layout.json)
    size=$(jq -er --arg p "$part" '.partitions[$p].bytes' layout.json)
    mib=$(jq -er --arg p "$part" '.filesystemMiB[$p]' "$recipe")
    [[ "$mib" =~ ^[0-9]{1,4}$ ]] && ((mib > 0 && mib <= 8192)) || die "Invalid size for $part"
    dd if="$base/system.img" of="$image" bs=4M iflag=skip_bytes,count_bytes skip="$off" count="$size" conv=sparse status=none
    info=$(dumpe2fs -h "$image" 2>/dev/null)
    block=$(awk '/^Block size:/{print $3}' <<<"$info")
    count=$(awk '/^Block count:/{print $3}' <<<"$info")
    ((count > 0 && block == 4096 && count*block <= size && count*block < mib*1048576)) || die "Invalid ext4 geometry for $part"
    truncate -s "$((count*block))" "$image"
    checkfs "$image"
    truncate -s "${mib}M" "$image"
    resize2fs "$image" "${mib}M" >> evidence/resize.log 2>&1
    rc=0; e2fsck -fy -E unshare_blocks "$image" >> evidence/resize.log 2>&1 || rc=$?
    ((rc <= 1)) || die "Cannot unshare $part"
    checkfs "$image"
done
# debugfs can return success for a failed subcommand.
debug() {
    local out
    out=$(debugfs -R "$1" "$image" 2> debug.err) || return 1
    [[ -z "$(sed '/^debugfs [0-9]/d' debug.err)" ]] || return 1
    printf '%s\n' "$out"
}
mutate() {
    debugfs -w -f ops "$image" >/dev/null 2> debug.err
    [[ -z "$(sed '/^debugfs [0-9]/d' debug.err)" ]] || die "$(cat debug.err)"
}
validpath() { [[ "$1" =~ ^/[a-zA-Z0-9_.+@/%:=,-]+$ && "/${1#/}/" != *'/../'* && "/${1#/}/" != *'/./'* && "$1" != *'//'* ]]; }
loaded() {
    if metadata=$(debug "stat $1"); then return 0; fi
    grep -q 'File not found by ext2_lookup' debug.err || die "Cannot inspect $1"
    return 1
}
dump() { rm -f readback; debug "dump $1 readback" >/dev/null && [[ -f readback ]] || die "Cannot read $1"; }
plat=$(jq -er '.install[]|select(.target=="/system/etc/selinux/plat_file_contexts")|.source.file' plan.json)
plat_pin=$(jq -er '.install[]|select(.target=="/system/etc/selinux/plat_file_contexts")|.source.sha256' plan.json)
[[ "$(checksum "$plat")" == "$plat_pin" ]] || die 'Platform context bytes changed'
cat "$plat" > contexts
for part in system_ext product vendor; do
    image="output/$part.ext4"
    path="/etc/selinux/${part}_file_contexts"
    if loaded "$path"; then dump "$path"; printf '\n' >> contexts; cat readback >> contexts; fi
done
attr() {
    local path=$1 kind=$2 uid=$3 gid=$4 mode=$5 android=$1 type=0100
    [[ "$part" == system ]] || android="/$part$path"
    context=$(matchpathcon -N -n -m "$kind" -f contexts "$android")
    [[ "$context" =~ ^u:object_r:[A-Za-z0-9_]+:s0$ ]] || die "Unknown security context for $android"
    printf '%s\0' "$context" > label
    [[ "$kind" == file ]] || type=040
    printf 'set_inode_field %s uid %s\nset_inode_field %s gid %s\nset_inode_field %s mode %s%s\nea_set -f label %s security.selinux\n' \
        "$path" "$uid" "$path" "$gid" "$path" "$type" "${mode#0}" "$path" >> ops
}
attributes_match() {
    loaded "$1" && [[ "$metadata" =~ Mode:[[:space:]]+$4[[:space:]] && "$metadata" =~ User:[[:space:]]+$2[[:space:]]+Group:[[:space:]]+$3[[:space:]] ]] || die "Inode metadata differs for $1"
    rm -f readback-label
    debug "ea_get -f readback-label $1 security.selinux" >/dev/null
    cmp -s -- label readback-label || die "SELinux context differs for $1"
}
parents() {
    local parent=${1%/*}
    [[ -n "$parent" ]] || return 0
    parents "$parent"
    if loaded "$parent"; then [[ "$metadata" == *'Type: directory'* ]] || die "Not a directory $parent"; return; fi
    printf 'mkdir %s\n' "$parent" > ops; attr "$parent" dir 0 0 0755; mutate
    attributes_match "$parent" 0 0 0755
}
store() {
    local path=$1 file=$2 owner=$3 group=$4 mode=$5 expected=$6
    validpath "$path" && [[ "$owner" =~ ^[0-9]+$ && "$group" =~ ^[0-9]+$ && "$mode" =~ ^0[0-7]{3}$ && "$expected" =~ ^[a-f0-9]{64}$ ]] || die 'Invalid install record'
    parents "$path"
    : > ops
    if loaded "$path"; then
        [[ "$metadata" == *'Type: regular'* ]] || die "Refusing non-regular $path"
        xattrs=$(debug "ea_list $path")
        [[ -z "$(sed -nE '/^ +[^ ]+ \([0-9]+\)/ { /security\.selinux/!p; }' <<<"$xattrs")" ]] || die "Unmapped existing xattr for $path"
        printf 'rm %s\n' "$path" >> ops
    fi
    cp --reflink=auto -- "$file" payload
    [[ "$(checksum payload)" == "$expected" ]] || die "Changed payload $path"
    printf 'write payload %s\n' "$path" >> ops
    attr "$path" file "$owner" "$group" "$mode"; mutate; attributes_match "$path" "$owner" "$group" "$mode"
    dump "$path"; [[ "$(checksum readback)" == "$expected" ]] || die "File readback differs for $path"
    printf '%s\t%s\t%s\t%s\t%s:%s\t%s\t%s\n' "$part" "$path" "$selected" "$expected" "$owner" "$group" "$mode" "$context" >> evidence/install.tsv
}
map_target() {
    case "$1" in
        /system/product/*) part=product; path=${1#/system/product};;
        /system/system_ext/*) part=system_ext; path=${1#/system/system_ext};;
        /product/*|/system_ext/*|/vendor/*) part=${1#/}; part=${part%%/*}; path=${1#/"$part"};;
        /system/*) part=system; path=$1;;
        *) die "Unsupported target $1";;
    esac
    image="output/$part.ext4"
}

# Read the exact prepared files and pin each byte sequence again immediately before installation.
jq -er '.install[]|[.target,.source.file,.source.sha256,.mode,(.uid//0),(.gid//0),.artifact]|@tsv' plan.json > entries
declare -A covered=()
while IFS=$'\t' read -r target source source_pin mode uid gid artifact; do
    validpath "$target" || die 'Invalid guest path'
    map_target "$target"
    key="$part:$path"
    [[ -z "${covered[$key]+present}" ]] || die "Duplicate install $target"
    covered[$key]=1
    [[ "$(checksum "$source")" == "$source_pin" ]] || die "Changed source $target"
    selected=$artifact
    if jq -e --arg p "$target" '.keepPlatformElf|index($p)!=null' "$recipe" >/dev/null; then
        dump "$path"
        readelf -h readback | grep 'Machine:.*X86-64' >/dev/null || die "Expected the x86 platform ELF at $target"
        readelf -h "$source" | grep 'Machine:.*AArch64' >/dev/null || die "Unexpected OEM ELF for $target"
        printf '%s\t%s\t%s\t%s\t%s\n' "$part" "$path" keep-platform-elf "$(checksum readback)" "$source_pin" >> evidence/install.tsv
        continue
    fi
    if jq -e --arg p "$target" '.keepSdkFiles|has($p)' "$recipe" >/dev/null; then
        expected=$(jq -er --arg p "$target" '.keepSdkFiles[$p].artifact' "$recipe")
        [[ "$artifact" == "$expected" ]] || die "Unexpected artifact at $target"
        selected=$(jq -er --arg p "$target" '.keepSdkFiles[$p].selection' "$recipe")
        dump "$path"
        printf '%s\t%s\t%s\t%s\t%s\n' "$part" "$path" "$selected" "$(checksum readback)" "$source_pin" >> evidence/install.tsv
        continue
    fi
    if [[ "$target" == "$(jq -er .mergeFstab.target "$recipe")" ]]; then
        dump "$path"
        device=$(jq -er .mergeFstab.device "$recipe")
        mapfile -t rows < <(awk -v dev="$device" '$1==dev' "$source")
        [[ ${#rows[@]} == 1 ]] && grep -q '^/dev/block/pci/' readback || die 'Expected one OEM USB rule and SDK PCI mounts'
        if awk -v dev="$device" '$1==dev{found=1} END{exit !found}' readback; then die 'Duplicate USB mount rule'; fi
        cp -- readback combined-fstab
        printf '%s\n' "${rows[0]}" >> combined-fstab
        source=combined-fstab; selected=merge-usb-rule
    fi
    store "$path" "$source" "$uid" "$gid" "$mode" "$(checksum "$source")"
    [[ "$path" == *.apk ]] || continue
    # System APKs with extractNativeLibs enabled require the ISA-specific adjacent lib tree.
    unzip -Z1 "$source" > entries.ziplist
    grep -E '^lib/(x86|x86_64)/lib[A-Za-z0-9_+.@-]+\.so$' entries.ziplist > libs || :
    if [[ -s libs ]]; then
        "$aapt2" dump xmltree "$source" --file AndroidManifest.xml > manifest.tree
        if ! grep -Eq 'extractNativeLibs\(.*(false|0x0)$' manifest.tree; then
            while IFS=/ read -r _ isa name; do
                unzip -p "$source" "lib/$isa/$name" > lib.so
                machine='X86-64'; [[ "$isa" != x86 ]] || machine='Intel 80386'
                readelf -h lib.so | grep "Machine:.*$machine" >/dev/null || die "Wrong JNI ISA: $isa/$name"
                selected="$artifact:lib/$isa/$name"
                store "${path%/*}/lib/$isa/$name" lib.so 0 0 0644 "$(checksum lib.so)"
            done < libs
        fi
    fi
    # An APK displaced from the SDK must not retain products of its previous dex.
    printf '%s\t%s\n' "$part" "$path" >> apps
done < entries
clear_preopt() {
    local path=$1
    if loaded "$path"; then
        [[ "$metadata" == *'Type: regular'* ]] || die "Unexpected preopt inode $path"
        dump "$path"; printf '%s\t%s\t%s\n' "$part" "$path" "$(checksum readback)" >> evidence/old-preopt.tsv
        printf 'rm %s\n' "$path" > ops; mutate
        if loaded "$path"; then die "Old preopt survived $path"; fi
    fi
}
# Changed JAR/APK inputs must contain DEX before removing their originally shipped preopt.
while IFS=$'\t' read -r part path; do
    image="output/$part.ext4"
    leaf=${path##*/}; leaf=${leaf%.*}
    dump "$path"
    unzip -Z1 readback > ziplist
    if ! grep -Eq '^classes([2-9]|[1-9][0-9]+)?\.dex$' ziplist; then
        [[ "$path" == *.apk ]] || die "Missing classpath DEX in $path"
        continue
    fi
    for isa in x86 x86_64; do
        for suffix in art odex vdex; do clear_preopt "${path%/*}/oat/$isa/$leaf.$suffix"; done
    done
done < <(cat apps; jq -r '.changedSystemServerJars[]|"system\t/system/framework/\(.).jar"' "$recipe")
for part in "${parts[@]}"; do checkfs "output/$part.ext4"; done
"$inputs" "${args[@]}" > revalidated.json
cmp -s plan.json revalidated.json || die 'Source generation changed while creating filesystems'
cp -- plan.json layout.json "$recipe" evidence/
mv -- evidence output/
(cd output && sha256sum -- system.ext4 system_ext.ext4 product.ext4 vendor.ext4 > filesystems.sha256)
mv -T -- output "$dest"
printf '%s\n' "$dest"
