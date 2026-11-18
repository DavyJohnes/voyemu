#!/bin/bash
set -euo pipefail
# Package one immutable Git commit. Network access requires QNX_SOURCE_DOWNLOAD=1.
[[ $# -ge 3 && $# -le 4 ]] || { echo 'Usage: qnx-host-sources.sh HOST COMMIT OUTPUT_DIR [CACHE_DIR]' >&2; exit 2; }
host=$1
case "$host" in
    macos-arm64) component=macos_arm64 ;;
    linux-x64) component=linux_x64 ;;
    windows-x64) component=windows_file_backend ;;
    *) echo "Unsupported source host: $host" >&2; exit 2 ;;
esac
commit=$(git rev-parse --verify "$2^{commit}")
project=$(git rev-parse --show-toplevel)
mkdir -p "$3"
out=$(cd "$3" && pwd -P)
cache=${4:-$project/.local/toolchains/downloads/qnx-host}
mkdir -p "$project/.local/build/jobs"
work=$(mktemp -d "$project/.local/build/jobs/qnx-host-sources.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
payload="$work/payload"
mkdir -p "$payload/inputs"
git show "$commit:src/app/qnx-host.json" > "$work/catalog.json"
supplement=
case "$host" in
    macos-arm64) supplement=qnx-host-macos-sources.json ;;
    windows-x64) supplement=qnx-host-windows-sources.json ;;
esac
if [[ -n "$supplement" ]]; then
    git show "$commit:src/app/$supplement" > "$work/supplement.json" || {
        echo "Exact dependency source recipes are missing: $supplement" >&2; exit 1;
    }
    jq -e --arg key "$component" --slurpfile catalog "$work/catalog.json" '
        . as $lock |
        .schema == 1 and (.sources | length > 0) and
        ([.sources[].name] | length == (unique | length)) and
        all($catalog[0][$key].dependencies[];
            (if $key == "macos_arm64" then .bottle.sha256 else .sha256 end) as $binary |
            ($lock.packages[$binary] | if type == "string" then [.] else . end) as $names |
            ($names | type == "array" and length > 0) and
            all($names[]; . as $name | any($lock.sources[]; .name == $name)))
    ' "$work/supplement.json" >/dev/null || {
        echo "Dependency source lock does not cover exact binary inputs: $supplement" >&2; exit 1;
    }
fi
jq -er --arg key "$component" '
    .[$key] as $c |
    ([{name:"qemu", spec:$c.source}] +
     (if $key == "windows_file_backend" then [] else [$c.dependencies | to_entries[] | {name:.key, spec:(.value.source // .value)}] + [{name:"pkgconf",spec:$c.pkgconf}] end) +
     (if $key == "windows_file_backend" then [{name:"dtc",spec:$c.dtc}] else [] end))[] |
    if (.spec.url | type) == "string" and (.spec.sha256 | test("^[0-9a-f]{64}$"))
    then [.name,.spec.sha256,.spec.url] | @tsv
    else error("invalid source pin") end
' "$work/catalog.json" > "$payload/SOURCES.tsv"
if [[ -n "$supplement" ]]; then
    jq -er '.sources[] | if (.url | type) == "string" and (.sha256 | test("^[0-9a-f]{64}$")) then [.name,.sha256,.url] | @tsv else error("invalid supplementary source pin") end' "$work/supplement.json" >> "$payload/SOURCES.tsv"
fi
sha256() { shasum -a 256 "$1" | cut -d' ' -f1; }
while IFS=$'\t' read -r name sha url; do
    base=${url##*/}
    [[ "$base" =~ ^[A-Za-z0-9._+-]+$ ]] || { echo "Unsupported source filename: $base" >&2; exit 1; }
    input=
    if [[ ! -f "$cache/$sha.blob" && ${QNX_SOURCE_DOWNLOAD:-0} == 1 ]]; then
        mkdir -p "$cache"
        curl --fail --location --proto '=https' --tlsv1.2 --output "$work/download" "$url"
        [[ $(sha256 "$work/download") == "$sha" ]] || { echo "Source SHA-256 mismatch: $name" >&2; exit 1; }
        mv "$work/download" "$cache/$sha.blob"
    fi
    for candidate in "$cache/$sha.blob" "$cache/$sha" "$cache/$sha-$base" "$cache/$sha.${base#*.}" "$cache/$base"; do
        if [[ -f "$candidate" && ! -L "$candidate" ]]; then input=$candidate; break; fi
    done
    [[ -n "$input" ]] || { echo "Missing source input: $name $sha ($base)" >&2; exit 1; }
    [[ $(sha256 "$input") == "$sha" ]] || { echo "Source SHA-256 mismatch: $name" >&2; exit 1; }
    cp "$input" "$payload/inputs/$sha-$base"
done < "$payload/SOURCES.tsv"
# Positive allowlist: source, build configuration, workflow and repository license only.
# A nested archive retains the frozen scripts without including profiles or machine data.
paths=()
for path in src config .github CMakeLists.txt LICENSE; do
    if git cat-file -e "$commit:$path" 2>/dev/null; then paths+=("$path"); fi
done
[[ ${#paths[@]} -gt 0 ]]
git -c core.autocrlf=false -c core.attributesfile=/dev/null -c tar.umask=0022 archive --format=tar --prefix=voyemu/ "$commit" -- "${paths[@]}" > "$payload/voyemu.tar"
printf '%s\n' "$commit" > "$payload/COMMIT"
cat > "$payload/README.txt" <<EOF_README
QNX host corresponding-source companion: $host

The binary companion is qnx-host-$host.zip in the same resource release.
COMMIT identifies the immutable Voyemu recipe snapshot in voyemu.tar.
SOURCES.tsv records each included upstream source URL and SHA-256.
inputs/ contains the complete upstream source archives, including their licenses.
SHA256SUMS verifies all included files; this archive has a separate .sha256 file.

Extract voyemu.tar. The actual build and installation recipes are in
voyemu/src/app/qnx/qnx_host_{macos,linux,windows}.cpp and src/app/qnx-host.json;
the CI commands and toolchain setup are in voyemu/.github/workflows/qnx-host.yml
and voyemu/.github/actions/toolchains. Patches are in voyemu/src/app/*.patch.
Follow the host's Build the recipe runner and Build the engine from the recipe
steps in that workflow. Use the frozen src/app/qnx-host.json, not a newer checkout.
To reuse included sources, copy each inputs/SHA256-filename to
voyemu/.local/toolchains/downloads/qnx-host/SHA256.blob before running the recipe.
The recipe also needs the compiler, build tools and any binary dependency inputs
pinned by that catalog; these are separate from this corresponding-source archive.
For macOS, dependencies use the exact pinned Homebrew bottles. The source lock
qnx-host-macos-sources.json supplies their formula and patch inputs in addition
to upstream archives. For Windows, qnx-host-windows-sources.json maps each
binary package hash to its MSYS2 source package with build recipes and patches.
These locks are included in the frozen recipe snapshot and SOURCES.tsv.
No proprietary SDK, OEM input or machine state is included.
EOF_README
(
    cd "$payload"
    find . -type f ! -name SHA256SUMS | LC_ALL=C sort | while IFS= read -r file; do
        printf '%s  %s\n' "$(sha256 "$file")" "${file#./}"
    done > SHA256SUMS
)
# Git's archive writer provides stable ordering, modes, uid/gid and timestamps on
# every supported packaging host, without relying on a platform-specific tar.
git init -q --template= --object-format=sha1 "$work/archive-git"
find "$payload" -type f -exec chmod 644 {} +
git -c core.autocrlf=false -c core.attributesfile=/dev/null --git-dir="$work/archive-git/.git" --work-tree="$payload" add --all --force
index_tree=$(git --git-dir="$work/archive-git/.git" write-tree)
archive_commit=$(printf 'QNX corresponding sources\n' | env GIT_AUTHOR_NAME=Voyemu GIT_AUTHOR_EMAIL=source@voyemu.invalid GIT_COMMITTER_NAME=Voyemu GIT_COMMITTER_EMAIL=source@voyemu.invalid GIT_AUTHOR_DATE='2000-01-01T00:00:00Z' GIT_COMMITTER_DATE='2000-01-01T00:00:00Z' git --git-dir="$work/archive-git/.git" commit-tree "$index_tree")
asset="qnx-host-$host-sources.tar"
git -c core.autocrlf=false -c core.attributesfile=/dev/null -c tar.umask=0022 --git-dir="$work/archive-git/.git" archive --format=tar --prefix="qnx-host-$host-sources/" "$archive_commit" > "$work/$asset"
printf '%s  %s\n' "$(sha256 "$work/$asset")" "$asset" > "$work/$asset.sha256"
mv "$work/$asset" "$out/$asset"
mv "$work/$asset.sha256" "$out/$asset.sha256"
printf 'Prepared %s\n' "$out/$asset"
