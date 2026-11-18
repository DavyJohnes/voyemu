#!/bin/bash
set -euo pipefail
# Exercise the packager against frozen, synthetic source trees; no network or build.
project=$(cd "$(dirname "$0")/../../.." && pwd -P)
packager="$project/src/dev/ci/qnx-host-sources.sh"
mkdir -p "$project/.local/build/jobs"
work=$(mktemp -d "$project/.local/build/jobs/qnx-host-sources-test.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/repo/src/app" "$work/cache" "$work/repo/config" "$work/repo/plans" "$work/repo/profiles/private"
cd "$work/repo"
git init -q
git config user.name SourceTest
git config user.email source-test@example.invalid
printf 'qemu source fixture\n' > "$work/input"
sha=$(shasum -a 256 "$work/input" | cut -d' ' -f1)
cp "$work/input" "$work/cache/$sha.blob"
spec="{\"url\":\"https://example.invalid/source.tar.xz\",\"sha256\":\"$sha\"}"
printf '{"linux_x64":{"source":%s,"pkgconf":%s,"dependencies":{"glib":%s}},"macos_arm64":{"source":%s,"pkgconf":%s,"dependencies":{"glib":{"bottle":%s,"source":%s}}},"windows_file_backend":{"source":%s,"dtc":%s,"dependencies":{"glib2":{"sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}}}}\n' "$spec" "$spec" "$spec" "$spec" "$spec" "$spec" "$spec" "$spec" "$spec" > src/app/qnx-host.json
printf 'frozen recipe\n' > src/recipe.cpp
printf 'license\n' > LICENSE
printf 'private\n' > plans/private.txt
printf 'private\n' > profiles/private/data
printf '{"schema":1,"sources":[{"name":"formula","url":"https://example.invalid/source.tar.xz","sha256":"%s"}],"packages":{"%s":"formula"}}\n' "$sha" "$sha" > src/app/qnx-host-macos-sources.json
git add .
git commit -qm fixture
commit=$(git rev-parse HEAD)
printf 'dirty recipe\n' > src/recipe.cpp
printf 'untracked extra\n' > src/extra.cpp
printf '{}\n' > src/app/qnx-host.json
bash "$packager" linux-x64 "$commit" "$work/out1" "$work/cache"
printf '* text eol=crlf\n' > "$work/global-attributes"
GIT_CONFIG_COUNT=3 GIT_CONFIG_KEY_0=core.autocrlf GIT_CONFIG_VALUE_0=true GIT_CONFIG_KEY_1=tar.umask GIT_CONFIG_VALUE_1=0077 GIT_CONFIG_KEY_2=core.attributesFile GIT_CONFIG_VALUE_2="$work/global-attributes" bash "$packager" linux-x64 "$commit" "$work/out2" "$work/cache"
cmp "$work/out1/qnx-host-linux-x64-sources.tar" "$work/out2/qnx-host-linux-x64-sources.tar"
mkdir "$work/extract"
tar -xf "$work/out1/qnx-host-linux-x64-sources.tar" -C "$work/extract"
payload="$work/extract/qnx-host-linux-x64-sources"
(cd "$payload" && shasum -a 256 -c SHA256SUMS)
(cd "$work/out1" && shasum -a 256 -c qnx-host-linux-x64-sources.tar.sha256)
[[ $(cat "$payload/COMMIT") == "$commit" ]]
[[ $(tar -xOf "$payload/voyemu.tar" voyemu/src/recipe.cpp) == 'frozen recipe' ]]
git show "$commit:src/app/qnx-host.json" > "$work/frozen-catalog"
tar -xOf "$payload/voyemu.tar" voyemu/src/app/qnx-host.json > "$work/archived-catalog"
cmp "$work/frozen-catalog" "$work/archived-catalog"
if tar -tf "$payload/voyemu.tar" | grep -qx voyemu/src/extra.cpp; then exit 1; fi
if tar -tf "$payload/voyemu.tar" | grep -Eq 'voyemu/(plans|profiles|\.local|machines)/'; then echo 'Private path packaged' >&2; exit 1; fi
[[ $(cat "$payload/inputs/$sha-source.tar.xz") == 'qemu source fixture' ]]
bash "$packager" macos-arm64 "$commit" "$work/mac" "$work/cache"
reject() {
    if bash "$packager" "$1" "$commit" "$work/rejected" "$work/cache" > "$work/rejected.log" 2>&1; then echo "Accepted invalid $2" >&2; exit 1; fi
    [[ ! -f "$work/rejected/qnx-host-$1-sources.tar" ]]
    echo "PASS reject $2"
}
reject windows-x64 missing-windows-source-lock
# Windows packages ship binary DLLs; require the separate source-only package.
printf 'source-only package: PKGBUILD and patches\n' > "$work/windows-source"
windows_sha=$(shasum -a 256 "$work/windows-source" | cut -d' ' -f1)
cp "$work/windows-source" "$work/cache/$windows_sha.blob"
printf '{"schema":1,"sources":[{"name":"glib2-source","url":"https://example.invalid/glib2.src.tar.zst","sha256":"%s"}],"packages":{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa":"glib2-source"}}\n' "$windows_sha" > src/app/qnx-host-windows-sources.json
git add src/app/qnx-host-windows-sources.json
git commit -qm windows-source-lock
commit=$(git rev-parse HEAD)
bash "$packager" windows-x64 "$commit" "$work/windows" "$work/cache"
tar -xOf "$work/windows/qnx-host-windows-x64-sources.tar" qnx-host-windows-x64-sources/SOURCES.tsv > "$work/windows-sources.tsv"
printf 'glib2-source\t%s\thttps://example.invalid/glib2.src.tar.zst\n' "$windows_sha" > "$work/expected-windows-source"
grep -Fx -f "$work/expected-windows-source" "$work/windows-sources.tsv"
tar -xOf "$work/windows/qnx-host-windows-x64-sources.tar" "qnx-host-windows-x64-sources/inputs/$windows_sha-glib2.src.tar.zst" > "$work/archived-windows-source"
cmp "$work/windows-source" "$work/archived-windows-source"
printf '{"schema":1,"sources":[{"name":"glib2-source","url":"https://example.invalid/glib2.src.tar.zst","sha256":"%s"}],"packages":{}}\n' "$windows_sha" > src/app/qnx-host-windows-sources.json
git add src/app/qnx-host-windows-sources.json
git commit -qm mismatched-windows-lock
commit=$(git rev-parse HEAD)
reject windows-x64 mismatched-windows-binary-source-binding
# A lock that names another binary is insufficient even when its sources exist.
printf '{"schema":1,"sources":[{"name":"formula","url":"https://example.invalid/source.tar.xz","sha256":"%s"}],"packages":{}}\n' "$sha" > src/app/qnx-host-macos-sources.json
git add src/app/qnx-host-macos-sources.json
git commit -qm mismatched-lock
commit=$(git rev-parse HEAD)
reject macos-arm64 mismatched-binary-source-binding
# Failed packaging preserves an existing artifact in the destination.
cp "$work/out1/qnx-host-linux-x64-sources.tar" "$work/rejected/qnx-host-linux-x64-sources.tar"
mv "$work/cache/$sha.blob" "$work/saved"
if bash "$packager" linux-x64 "$commit" "$work/rejected" "$work/cache" > "$work/rejected.log" 2>&1; then exit 1; fi
cmp "$work/out1/qnx-host-linux-x64-sources.tar" "$work/rejected/qnx-host-linux-x64-sources.tar"
rm "$work/rejected/qnx-host-linux-x64-sources.tar"
reject linux-x64 missing-source
printf 'tampered\n' > "$work/cache/$sha.blob"
reject linux-x64 tampered-source
printf 'PASS frozen snapshot, deterministic archive, manifests, source bytes and source-lock checks\n'
