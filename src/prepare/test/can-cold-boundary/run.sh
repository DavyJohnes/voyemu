#!/usr/bin/env bash
set -euo pipefail
umask 077
repo=$(cd "$(dirname "$0")/../../../.." && pwd -P)
tests="$(cd "$(dirname "$0")" && pwd -P)"
mkdir -p "$repo/.local/build/jobs"
boundary_test_dir=$(mktemp -d "$repo/.local/build/jobs/can-boundary-host-test.XXXXXXXX")
trap 'rm -rf "$boundary_test_dir"' EXIT
# Every profile simulator and observer must speak the boundary ABI of its development kernel.
for recipe in \
    free-09-chinese-20260106-6.0.3/components/can-simulator:free-09-chinese-20260106-6.0.3/components/can-model-observer \
    free-11-chinese-20260731-7.3.1/components/can-simulator:free-11-chinese-20260731-7.3.1/components/can-model-observer \
    free-11-chinese-20250710-6.11.1/components/can-simulator:free-11-chinese-20250710-6.11.1/components/can-model-observer; do
    common="$repo/profiles/${recipe%%:*}/src"
    observer="$repo/profiles/${recipe#*:}/src"
    for test in "$tests"/*.test.c; do
        name=$(basename "$test" .test.c)
        ${CC:-cc} -D_DARWIN_C_SOURCE -D_DEFAULT_SOURCE -std=c11 -Wall -Wextra -Werror -Wno-sign-compare \
            -I"$common" -I"$observer" \
            "$test" "$common/boot_id.c" "$common/mcu_model.c" "$common/model_fixture.c" \
            "$common/model_oracle.c" "$common/model_protocol.c" "$common/model_queue.c" \
            "$common/observer_protocol.c" -o "$boundary_test_dir/$name"
        "$boundary_test_dir/$name"
        printf '%s %s PASS\n' "${recipe%%/*}" "$name"
    done
done
