#!/bin/bash
# Reuse the checked artifact, never one of the individual build job outputs.
set -euo pipefail
run=$1
host=$2
asset=$3
pin=$4
commit=$5
out=$6
repo=$7
[[ "$repo" == voyemu/voyemu || "$repo" == voyemu/voyemu-deps ]]
[[ "$run" =~ ^[0-9]+$ && "$asset" == "qnx-host-$host.zip" && "$pin" =~ ^[0-9a-f]{64}$ ]]

metadata=$(gh api "repos/$repo/actions/runs/$run")
producer=$(jq -er --arg repo "$repo" '
    select(.conclusion == "success" and .event == "workflow_dispatch" and
           .head_repository.full_name == $repo and .path == ".github/workflows/qnx-host.yml") |
    .head_sha | select(test("^[0-9a-f]{40}$"))
' <<< "$metadata")
artifacts=$(gh api --paginate --slurp "repos/$repo/actions/runs/$run/artifacts?per_page=100")
name=$(jq -er --arg prefix "qnx-host-$host" '
    [.[].artifacts[] | select(.expired == false and
        (.name == $prefix or (.name | startswith($prefix + "-") and test("-[0-9a-f]{8}$")))) | .name] |
    if length == 0 then "" elif length == 1 then .[0] else error("ambiguous checked artifacts") end
' <<< "$artifacts")

if [[ -z "$name" ]]; then
    echo "Checked $host artifact unavailable in run $run; build once against the pin."
    echo "reused=false" >> "$GITHUB_OUTPUT"
    exit 0
fi

# Only the publication pin and Windows executable identity can be added after
# the producer run. Require exactly the producer tree for all other inputs.
git fetch --no-tags --depth=1 "https://github.com/$repo.git" "$producer"
git diff --exit-code "$producer" "$commit" -- . ':(exclude)src/app/qnx-host.json'
recipe() {
    git show "$1:src/app/qnx-host.json" | jq -S '
        del(.prebuilt,
            .windows_file_backend.verified_runtime.files["qemu-system-aarch64.exe"],
            .windows_file_backend.verified_runtime.qnx_file_backend.engine_sha256)
    '
}
producer_recipe=$(recipe "$producer")
release_recipe=$(recipe "$commit")
diff -u <(printf '%s\n' "$producer_recipe") <(printf '%s\n' "$release_recipe")

gh run download "$run" --repo "$repo" --name "$name" --dir "$out"
jq -e --arg producer "$producer" --arg asset "$asset" --arg pin "$pin" '
    .schema == 1 and .commit == $producer and .asset == $asset and .sha256 == $pin
' "$out/build.json" >/dev/null
jq -e --arg producer "$producer" --arg host "$host" --arg pin "$pin" '
    .schema == 1 and .commit == $producer and .host == $host and
    .sha256 == $pin and .reproducible == true
' "$out/verification.json" >/dev/null
printf '%s  %s\n' "$pin" "$out/$asset" | sha256sum -c -
echo "reused=true" >> "$GITHUB_OUTPUT"
