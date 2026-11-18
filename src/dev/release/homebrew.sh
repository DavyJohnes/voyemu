#!/bin/bash
# Writes the Homebrew formula of a release from src/dev/release/homebrew/voyemu.rb.in: the version, the
# repository of the release archives and the SHA-256 of the macOS arm64 and Linux x64 archives in
# SHA256SUMS. The release workflow commits the result as Formula/voyemu.rb of voyemu/homebrew-tap.
# Usage: homebrew.sh VERSION REPOSITORY SHA256SUMS OUTPUT
set -euo pipefail
version=${1:?version required}
repository=${2:?repository required}
sums=${3:?SHA256SUMS required}
output=${4:?output file required}
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$ ]] || { echo "Invalid version $version" >&2; exit 1; }
[[ "$repository" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]] || { echo "Invalid repository $repository" >&2; exit 1; }
sha() {
    local file="voyemu-$version-$1.tar.xz" value
    value=$(awk -v file="$file" '$2 == file { print $1 }' "$sums")
    [[ "$value" =~ ^[0-9a-f]{64}$ ]] || { echo "No SHA-256 of $file in $sums" >&2; exit 1; }
    echo "$value"
}
macos=$(sha macos-arm64)
linux=$(sha linux-x64)
sed -e "s|@VERSION@|$version|g" -e "s|@REPOSITORY@|$repository|g" \
    -e "s|@MACOS_ARM64_SHA256@|$macos|g" -e "s|@LINUX_X64_SHA256@|$linux|g" \
    "$(dirname "$0")/homebrew/voyemu.rb.in" > "$output"
if grep -q '@[A-Z0-9_]*@' "$output"; then
    echo "Unsubstituted placeholder in $output" >&2
    exit 1
fi
