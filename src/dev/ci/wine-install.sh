#!/bin/bash
# Wine is fixed to Ubuntu 24.04's maintained LTS package, rather than a floating WineHQ release.
# SHA-256 values are published at https://packages.ubuntu.com/noble/<arch>/<package>/download.
# Runtime dependencies come from the Ubuntu runner's signed APT repositories.
set -euo pipefail
cd "$(dirname "$0")/../../.."
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]]
version=9.0~repack-4build3
downloads="$PWD/.local/toolchains/downloads/wine-ci"
mkdir -p "$downloads"
packages=()
while read -r sha name; do
    file="$downloads/$name"
    curl -fsSL --retry 3 "https://archive.ubuntu.com/ubuntu/pool/universe/w/wine/$name" -o "$file"
    echo "$sha  $file" | sha256sum -c -
    packages+=("$file")
done << 'PACKAGES'
5606f0f5677ac0fb882e3fdc8ce03a60232cdf2930e912ed3e42db2b9e0d5f7f wine_9.0~repack-4build3_all.deb
7f4f912b21917f28c281c99e3244ee801be2e20f0a23cd9f6d37acc97f5041ff wine64_9.0~repack-4build3_amd64.deb
eb421a3660bfdc090d70ce16b06aadfde1c2bce2593292023e520988f45d009e libwine_9.0~repack-4build3_amd64.deb
PACKAGES
sudo apt-get update
sudo apt-get install -y --no-install-recommends "${packages[@]}"
for package in wine wine64 libwine; do
    [[ $(dpkg-query --show --showformat='${Version}' "$package") == "$version" ]]
done
wine --version
