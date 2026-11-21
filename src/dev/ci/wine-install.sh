#!/bin/bash
# Wine is fixed to Ubuntu 24.04's maintained LTS package, rather than a floating WineHQ release.
# optionalTools.linuxBuild.wine in src/app/toolchain.json pins each package by URL and SHA-256; the
# values are published at https://packages.ubuntu.com/noble/<arch>/<package>/download.
# Runtime dependencies come from the Ubuntu runner's signed APT repositories.
set -euo pipefail
cd "$(dirname "$0")/../../.."
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]]
pins=src/app/toolchain.json
version=$(jq -er .optionalTools.linuxBuild.wine.version "$pins")
downloads="$PWD/$(jq -er .optionalTools.linuxBuild.wine.directory "$pins")"
mkdir -p "$downloads"
packages=()
names=()
while IFS=$'\t' read -r name url sha; do
    file="$downloads/${url##*/}"
    curl -fsSL --retry 3 "$url" -o "$file"
    echo "$sha  $file" | sha256sum -c -
    packages+=("$file")
    names+=("$name")
done < <(jq -er '.optionalTools.linuxBuild.wine.packages[] | [.name, .url, .sha256] | @tsv' "$pins")
[[ ${#packages[@]} -gt 0 ]]
sudo apt-get update
sudo apt-get install -y --no-install-recommends "${packages[@]}"
for package in "${names[@]}"; do
    [[ $(dpkg-query --show --showformat='${Version}' "$package") == "$version" ]]
done
wine --version
