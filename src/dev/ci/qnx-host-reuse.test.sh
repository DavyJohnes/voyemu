#!/bin/bash
set -euo pipefail
project=$(cd "$(dirname "$0")/../../.." && pwd -P)
mkdir -p "$project/.local/build/jobs"
work=$(mktemp -d "$project/.local/build/jobs/qnx-host-reuse.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
cd "$work"
export GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null
git init -q
git config user.name Fixture
git config user.email fixture@example.invalid
mkdir -p src/app mock artifact
printf '{"prebuilt":{"windows_file_backend":null},"recipe":1}\n' > src/app/qnx-host.json
echo recipe > src/app/build-cmd
git add src/app && git commit -qm producer
export PRODUCER=$(git rev-parse HEAD)
export ARTIFACT="$work/artifact" GITHUB_OUTPUT="$work/output"
asset=qnx-host-windows-x64.zip
printf 'verified engine fixture\n' > "$ARTIFACT/$asset"
pin=$(shasum -a 256 "$ARTIFACT/$asset" | cut -d' ' -f1)
jq -n --arg sha "$pin" --arg asset "$asset" --arg commit "$PRODUCER" \
    '{schema: 1, sha256: $sha, asset: $asset, commit: $commit}' > "$ARTIFACT/build.json"
jq -n --arg sha "$pin" --arg commit "$PRODUCER" \
    '{schema: 1, sha256: $sha, host: "windows-x64", commit: $commit, reproducible: true}' > "$ARTIFACT/verification.json"
jq --arg pin "$pin" '.prebuilt.windows_file_backend = {sha256: $pin}' src/app/qnx-host.json > catalog
mv catalog src/app/qnx-host.json
git commit -qam pin
release=$(git rev-parse HEAD)

export FIXTURE_GIT=$(command -v git)
export EXPECTED_ASSET=$asset
cat > mock/git <<'MOCK'
#!/bin/bash
set -euo pipefail
if [[ "$1" == fetch ]]; then
    [[ "$*" == "fetch --no-tags --depth=1 https://github.com/voyemu/voyemu.git $PRODUCER" ]]
else
    exec "$FIXTURE_GIT" "$@"
fi
MOCK
cat > mock/gh <<'MOCK'
#!/bin/bash
set -euo pipefail
case "$*" in
    'api repos/voyemu/voyemu/actions/runs/123')
        jq -n --arg sha "$PRODUCER" --arg result "${RESULT:-success}" '{
            conclusion: $result, event:"workflow_dispatch", head_repository: {full_name:"voyemu/voyemu"},
            path:".github/workflows/qnx-host.yml", head_sha:$sha
        }'
        ;;
    'api --paginate --slurp repos/voyemu/voyemu/actions/runs/123/artifacts?per_page=100')
        jq -n --argjson expired "${EXPIRED:-false}" '[{artifacts:[
            {name:"qnx-host-windows-x64", expired:$expired},
            {name:"qnx-host-windows-x64-asset", expired:false},
            {name:"qnx-host-windows-x64-repeat", expired:false}
        ]}]'
        ;;
    'run download 123 --repo voyemu/voyemu --name qnx-host-windows-x64 --dir out')
        mkdir out
        cp "$ARTIFACT/$EXPECTED_ASSET" "$ARTIFACT/build.json" "$ARTIFACT/verification.json" out/
        ;;
    *) echo "Unexpected gh request $*" >&2; exit 1 ;;
esac
MOCK
cat > mock/sha256sum <<'MOCK'
#!/bin/bash
exec shasum -a 256 "$@"
MOCK
chmod +x mock/*
export PATH="$work/mock:$PATH"

run() {
    : > "$GITHUB_OUTPUT"
    bash "$project/src/dev/ci/qnx-host-reuse.sh" 123 windows-x64 "$asset" "$pin" "$1" out voyemu/voyemu
}
reject() {
    if run "$1" > rejected.log 2>&1; then
        echo "Successfully reused an invalid artifact" >&2
        exit 1
    fi
    test ! -s "$GITHUB_OUTPUT"
}
run "$release"
grep -qx 'reused=true' "$GITHUB_OUTPUT"
cmp "out/$asset" "$ARTIFACT/$asset"
rm -r out
EXPIRED=true run "$release"
grep -qx 'reused=false' "$GITHUB_OUTPUT"
test ! -e out
RESULT=failure reject "$release"
test ! -e out
echo changed > src/app/build-cmd
git commit -qam source-change
reject HEAD
test ! -e out
echo tampered >> "$ARTIFACT/$asset"
reject "$release"
echo 'QNX checked archive reuse and fallback fixtures passed'
