#!/bin/bash
# Writes the release notes of VERSION: the section of CHANGELOG under the heading "## VERSION" or
# "## VERSION — DATE" up to the next "## " heading. A missing or empty section is an error.
# Usage: notes.sh VERSION CHANGELOG OUTPUT
set -euo pipefail
version=${1:?version required}
changelog=${2:?changelog required}
output=${3:?output file required}
awk -v heading="## $version" '
    $0 == heading || index($0, heading " — ") == 1 { found = 1; next }
    found && /^## / { exit }
    found' "$changelog" > "$output"
grep -q '[^[:space:]]' "$output" || { echo "No release notes of $version in $changelog" >&2; exit 1; }
