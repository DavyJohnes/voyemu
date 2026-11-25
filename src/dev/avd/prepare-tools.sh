#!/usr/bin/env bash
set -euo pipefail
umask 077
export LC_ALL=C
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
root=$(cd -- "$here/../../.." && pwd -P)
pins="$here/config-avd-tools.tsv"
[[ -f "$pins" ]] || pins="$root/config/config-avd-tools.tsv"
id=$(sha256sum "$pins")
cache="$root/.local/toolchains/avd-image-tools/${id%% *}"
if [[ ${1:-} == --verify-only && $# == 1 ]]; then
    avbpin=$(awk -F '\t' '$1=="bin/avbtool" {print $2}' "$pins")
    if [[ ! -f "$cache/bin/avbtool" || -L "$cache/bin/avbtool" ||
          "$(sha256sum "$cache/bin/avbtool" | cut -d' ' -f1)" != "$avbpin" ]]; then
        echo 'Run prepare-tools.sh before verifying a release: pinned avbtool is missing or differs.' >&2
        exit 1
    fi
    printf '%s\n' "$cache"
    exit 0
fi
[[ $# == 0 ]] || { echo 'Expected no arguments or --verify-only' >&2; exit 2; }
mkdir -p -- "$cache"
exec 9>"$cache/install.lock"
flock 9
while IFS=$'\t' read -r name pin encoding url; do
    [[ -z "$name" || "$name" == "#"* ]] && continue
    file="$cache/$name"
    if [[ ! -f "$file" ]]; then
        if [[ ${VOYEMU_AVD_OFFLINE:-0} == 1 ]]; then
            echo "Missing pinned offline tool: $name" >&2
            exit 1
        fi
        mkdir -p -- "$(dirname -- "$file")"
        temp=$(mktemp "$cache/.download.XXXXXXXX")
        trap 'rm -f -- "$temp"' EXIT
        if [[ "$encoding" == base64 ]]; then
            curl --proto '=https' -fsSL "$url" | base64 -d > "$temp"
        else
            curl --proto '=https' -fsSL "$url" -o "$temp"
        fi
        [[ "$(sha256sum "$temp" | cut -d' ' -f1)" == "$pin" ]]
        chmod 700 "$temp"
        mv -T -- "$temp" "$file"
        trap - EXIT
    fi
    [[ -f "$file" && ! -L "$file" && "$(sha256sum "$file" | cut -d' ' -f1)" == "$pin" ]]
done < "$pins"
# Read the pristine Debian payloads each time.
for item in 'sgdisk gdisk.deb ./sbin/sgdisk' 'matchpathcon selinux-utils.deb ./usr/sbin/matchpathcon'; do
    read -r command package member <<< "$item"
    temp=$(mktemp "$cache/.binary.XXXXXXXX")
    trap 'rm -f -- "$temp"' EXIT
    dpkg-deb --fsys-tarfile "$cache/$package" | tar -xO "$member" > "$temp"
    chmod 700 "$temp"
    mv -T -- "$temp" "$cache/bin/$command"
    trap - EXIT
done
printf '%s\n' "$cache"
