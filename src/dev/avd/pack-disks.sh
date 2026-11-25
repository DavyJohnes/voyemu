#!/usr/bin/env bash
# Called after all AVB descriptors and signatures have been verified.
set -euo pipefail
export LC_ALL=C
[[ $# == 6 ]] || exit 2
source_sdk=$1
parts=$2
dest=$3
work=$4
layout_tool=$5
recipe=$6
die() { printf '%s\n' "$*" >&2; exit 1; }
mib=1048576
align() { printf '%s\n' "$(( ($1 + $2 - 1) / $2 * $2 ))"; }
size() { stat -c %s -- "$1"; }
copy_at() { dd if="$1" of="$2" bs=4M oflag=seek_bytes seek="$3" conv=notrunc status=none; }
checksum() { sha256sum "$1" | cut -d' ' -f1; }
names=(system system_ext product vendor)

partition_index() {
    partx --raw --noheadings -o NR,NAME "$1" | awk -v name="$2" '$2 == name {print $1}'
}

partition_field() {
    partx --raw --noheadings --nr "$2" -o "$3" "$1"
}

# Grow only a terminal GPT partition of our copy. Retain the original disk and partition GUIDs.
fit_partition() {
    local image=$1 label=$2 bytes=$3 index start sectors type guid highest end disk_sectors
    index=$(partition_index "$image" "$label")
    [[ "$index" =~ ^[1-9][0-9]*$ ]] || die "Missing unique GPT partition $label"
    start=$(partition_field "$image" "$index" START)
    sectors=$(partition_field "$image" "$index" SECTORS)
    [[ "$start" =~ ^[0-9]+$ && "$sectors" =~ ^[0-9]+$ ]] || die "Invalid GPT extent"
    if (( sectors * 512 >= bytes )); then return; fi
    highest=$(partx --raw --noheadings -o END "$image" | sort -n | tail -1)
    (( start + sectors - 1 == highest )) || die "Cannot grow non-terminal $label"
    [[ "$(partition_field "$image" "$index" FLAGS)" =~ ^(0x)?0*$ ]] || die "Unsupported GPT attributes"
    type=$(partition_field "$image" "$index" TYPE)
    guid=$(partition_field "$image" "$index" UUID)
    end=$(( start + (bytes + 511) / 512 - 1 ))
    disk_sectors=$((end + 1 + 2048))
    truncate -s "$((disk_sectors * 512))" "$image"
    sgdisk --move-second-header "$image" >&2
    sgdisk "--delete=$index" "--new=$index:$start:$end" "--typecode=$index:$type" \
        "--partition-guid=$index:$guid" "--change-name=$index:$label" "$image" >&2
    [[ "$(partition_field "$image" "$index" START)" == "$start" &&
       "$(partition_field "$image" "$index" UUID)" == "$guid" ]] || die "GPT identity changed"
}

range_matches() {
    local part=$1 disk=$2 offset=$3
    cmp --bytes="$(size "$part")" --ignore-initial="0:$offset" "$part" "$disk" ||
        die "Signed bytes differ from disk at $offset"
}

for target in "$dest/system.img" "$dest/vendor.img"; do
    [[ ! -e "$target" && ! -L "$target" ]] || die "Output already exists: $target"
done
original="$work/source-layout.json"
cp --reflink=auto -- "$source_sdk/system.img" "$dest/system.img"
cp --reflink=auto -- "$source_sdk/vendor.img" "$dest/vendor.img"
super_offset=$(jq -er '.super_offset' "$original")
super_size=$(jq -er '.super_bytes' "$original")
needed=$((4*mib))
same=true
args=()
group=$(jq -er '.group' "$recipe")
for name in "${names[@]}"; do
    bytes=$(size "$parts/$name.img")
    old=$(jq -er --arg name "$name" '.partitions[$name].bytes' "$original")
    ((bytes == old)) || same=false
    needed=$((needed + $(align "$bytes" "$mib")))
    args+=(--partition "$name:readonly:$bytes:$group" --image "$name=$parts/$name.img")
done

if [[ "$same" == true ]]; then
    for name in "${names[@]}"; do
        offset=$(jq -er --arg name "$name" '.partitions[$name].offset' "$original")
        copy_at "$parts/$name.img" "$dest/system.img" "$offset"
    done
else
    if (( needed > super_size )); then super_size=$(align "$needed" "$mib"); fi
    max_group=$((super_size - 2*mib))
    lpmake --metadata-size "$(jq -er '.metadataBytes' "$recipe")" \
        --metadata-slots "$(jq -er '.metadataSlots' "$recipe")" --super-name super \
        --device "super:$super_size:$mib:0" --group "$group:$max_group" \
        "${args[@]}" --output "$work/super.img"
    [[ "$(size "$work/super.img")" == "$super_size" ]] || die 'Incomplete super image'
    fit_partition "$dest/system.img" super "$super_size"
    copy_at "$work/super.img" "$dest/system.img" "$super_offset"
fi

index=$(partition_index "$dest/system.img" vbmeta)
start=$(partition_field "$dest/system.img" "$index" START)
capacity=$(partition_field "$dest/system.img" "$index" SECTORS)
(( $(size "$parts/vbmeta.img") <= capacity * 512 )) || die 'Signed vbmeta exceeds GPT partition'
dd if=/dev/zero of="$dest/system.img" bs=512 seek="$start" count="$capacity" conv=notrunc status=none
copy_at "$parts/vbmeta.img" "$dest/system.img" "$((start*512))"
range_matches "$parts/vbmeta.img" "$dest/system.img" "$((start*512))"

fit_partition "$dest/vendor.img" vendor "$(size "$parts/vendor.img")"
index=$(partition_index "$dest/vendor.img" vendor)
start=$(partition_field "$dest/vendor.img" "$index" START)
copy_at "$parts/vendor.img" "$dest/vendor.img" "$((start * 512))"
range_matches "$parts/vendor.img" "$dest/vendor.img" "$((start * 512))"
for disk in "$dest/system.img" "$dest/vendor.img"; do
    report=$(sgdisk --verify "$disk")
    [[ "$report" == *'No problems found.'* ]] || die "$report"
done
# Walk the actual published metadata, independently of lpmake's argv and write offsets.
"$layout_tool" "$dest/system.img" "$(checksum "$dest/system.img")" > "$work/packed-layout.json"
for name in "${names[@]}"; do
    offset=$(jq -er --arg name "$name" '.partitions[$name].offset' "$work/packed-layout.json")
    bytes=$(jq -er --arg name "$name" '.partitions[$name].bytes' "$work/packed-layout.json")
    [[ "$bytes" == "$(size "$parts/$name.img")" ]] || die "Wrong LP size for $name"
    range_matches "$parts/$name.img" "$dest/system.img" "$offset"
done
