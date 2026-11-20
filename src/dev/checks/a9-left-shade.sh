#!/usr/bin/env bash
# Run from the project root with an independently prepared A9 test machine.
set -euo pipefail

if [[ $# != 2 || ( $2 != home && $2 != setting ) ]]; then
    echo "Usage: bash src/dev/checks/a9-left-shade.sh TEST_MACHINE_PATH home|setting" >&2
    exit 2
fi

profile=free-09-chinese-20260106-6.0.3
launcher=com.qinggan.app.launcher
setting=com.qinggan.app.setting
machine=$(cd -- "$1" && pwd -P)
context=$2
case "${machine##*/}" in
    "$profile"|"$profile"-voboost*) echo "Supply a dedicated A9 test machine" >&2; exit 2 ;;
esac

voyemu=${VOYEMU:-./voyemu}
umask 077
mkdir -p .local/build/tmp
artifacts=$(mktemp -d "$(pwd -P)/.local/build/tmp/a9-left-shade-$context.XXXXXXXX")
echo "Evidence: $artifacts"
session=null
request_number=0

api() {
    local operation=$1
    local fields=${2:-'{}'}
    local response=$3
    request_number=$((request_number + 1))
    jq -nc --arg request "${artifacts##*/}-$request_number" --arg op "$operation" \
        --arg profile "$profile" --argjson session "$session" --argjson fields "$fields" '
        {schema: 1, request_id: $request, op: $op, profile: $profile}
        + (if $session == null then {} else {session: $session} end) + $fields
    ' | "$voyemu" test --request - > "$response" 2>> "$artifacts/voyemu.log"
    local status=$?
    (( status == 0 )) && jq -e '.ok == true' "$response" >/dev/null
}

collect_guest_diagnostic() (
    local adb=".local/toolchains/native-sdk/platform-tools/adb"
    local socket="localfilesystem:$artifacts/drawer-$1.sock"
    local port boot seen
    [[ -x $adb && -s $artifacts/closed.json ]] || return 1
    port=$(jq -er --arg id "$(jq -r .id <<< "$session")" '
        .result.last_observation.deliveries.android.owned_adb
        | select(.owner == $id and .adbd_port == (.console_port + 1))
        | .adbd_port | select(type == "number" and . >= 1024 and . <= 65535)
    ' "$artifacts/ready.json") || return 1
    boot=$(jq -er '.expected.boot_id' "$artifacts/closed.json") || return 1
    export ADB_LOCAL_TRANSPORT_MAX_PORT=1 ADB_MDNS_AUTO_CONNECT=-
    trap '"$adb" -L "$socket" kill-server; rm -f -- "${socket#localfilesystem:}"' EXIT
    "$adb" -L "$socket" start-server >&2 || return 1
    "$adb" -L "$socket" connect "127.0.0.1:$port" >&2 || return 1
    seen=$("$adb" -L "$socket" -s "127.0.0.1:$port" shell -T cat /proc/sys/kernel/random/boot_id) || return 1
    [[ $seen == "$boot" ]] || return 1
    if [[ $1 == routing ]]; then
        "$adb" -L "$socket" -s "127.0.0.1:$port" shell -T dumpsys -t 5 input | awk '
            /RecentQueue:/ { tail_lines = 16 }
            tail_lines > 0 { print; --tail_lines; next }
            /VehicleHiboardDelegate|Viewport.*displayId|TouchStatesByDisplay/ && lines++ < 50 { print }
        ' || return 1
    else
        "$adb" -L "$socket" -s "127.0.0.1:$port" shell -T logcat -d -b main \
            VehicleHiBoardBarNew:I VehicleHiBoardBar:I InputDispatcher:W "*:S" | tail -n 2000 || return 1
    fi
    seen=$("$adb" -L "$socket" -s "127.0.0.1:$port" shell -T cat /proc/sys/kernel/random/boot_id) || return 1
    [[ $seen == "$boot" ]]
)

cleanup() {
    local status=$?
    trap - EXIT
    if [[ -n ${routing_pid:-} ]]; then wait "$routing_pid" || true; fi
    if [[ $session != null ]]; then
        if (( status != 0 )); then
            collect_guest_diagnostic log > "$artifacts/oem-drawer.log" 2> "$artifacts/oem-drawer-adb.log" || true
            api session.screenshot "$(jq -nc --arg path "$artifacts/failure-screen" \
                '{screen: "primary", artifact_dir: $path, timeout_ms: 10000}')" \
                "$artifacts/failure-screenshot.json" || true
            api session.ui '{"screen":"primary","timeout_ms":10000}' \
                "$artifacts/failure-ui.json" || true
        fi
        if ! api session.stop '{"timeout_ms":60000}' "$artifacts/stop.json" ||
            ! jq -e '.result.cleanup.verified == true' "$artifacts/stop.json" >/dev/null; then
            echo "Failed to stop the owned test session. See $artifacts/stop.json" >&2
            status=1
        fi
    fi
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

api session.start "$(jq -nc --arg path "$machine" \
    '{machine_path: $path, scenario:"fresh", timeout_ms:120000}')" "$artifacts/start.json"
session=$(jq -ce '.result.session | {id, ownership_id} | select(.id and .ownership_id)' "$artifacts/start.json")
api session.wait '{"predicate":"android.ready","timeout_ms":60000}' "$artifacts/ready.json"
jq -e '.result.ready == true' "$artifacts/ready.json" >/dev/null

observe() {
    # The API28 default uses Accessibility window roles; all_attached uses raw WM types.
    api session.ui '{"screen":"primary","timeout_ms":5000}' "$1"
}

if [[ $context == setting ]]; then
    observe "$artifacts/before-launch.json"
    boot=$(jq -er '.result.multimedia[0].boot_id | strings | select(length > 0)' "$artifacts/before-launch.json")
    api multimedia.launch "$(jq -nc --arg package "$setting" --arg boot "$boot" \
        '{package:$package, boot_id:$boot, screen:"primary"}')" "$artifacts/launch.json"
    foreground=$setting
else
    foreground=$launcher
fi

select_ui() {
    jq -ce --arg stage "$1" --arg owner "$launcher" --arg foreground "$foreground" \
        -f src/dev/checks/a9-left-shade.jq "$2"
}

wait_for_ui() {
    local stage=$1
    local deadline=$((SECONDS + 35))
    local attempt=0
    local observation
    if [[ $stage == open ]]; then deadline=$((SECONDS + 8)); fi
    while (( SECONDS < deadline )); do
        attempt=$((attempt + 1))
        observation="$artifacts/$stage-ui.json"
        if [[ $stage == open ]]; then observation="$artifacts/open-ui-$attempt.json"; fi
        if observe "$observation" &&
            select_ui "$stage" "$observation" > "$artifacts/$stage.json"; then
            if [[ $stage == open ]]; then cp "$observation" "$artifacts/open-ui.json"; fi
            return 0
        fi
        sleep 1
    done
    echo "Timed out waiting for $stage in $context" >&2
    return 1
}

wait_for_ui closed
api input.gesture "$(jq -c 'del(.observed_end_ns)' "$artifacts/closed.json")" "$artifacts/gesture.json"
if [[ ${VOYEMU_SHADE_INPUT_TRACE:-0} == 1 ]]; then
    collect_guest_diagnostic routing > "$artifacts/input-routing.log" 2> "$artifacts/input-routing-adb.log" &
    routing_pid=$!
fi
jq -e '.result.pre_dispatch_fenced == true and .result.sdk_completed == true and
    .result.pointer_released == true and
    (.result.sent | length == 11)' \
    "$artifacts/gesture.json" >/dev/null
wait_for_ui open
jq -es '.[0].expected.boot_id == .[1].boot_id and
    .[0].expected.source_instance == .[1].source_instance and
    ([(.[0].observed_end_ns | tonumber), (.[2].result.sent[-1].completed_monotonic_us * 1000)] | max)
        <= (.[1].observed_start_ns | tonumber)' \
    "$artifacts/closed.json" "$artifacts/open.json" "$artifacts/gesture.json" >/dev/null
wait_for_ui reclosed
jq -es '.[0].boot_id == .[1].expected.boot_id and
    .[0].source_instance == .[1].expected.source_instance' \
    "$artifacts/open.json" "$artifacts/reclosed.json" >/dev/null
echo "PASS: OEM Drive Mode card is visible after opening the left shade from $context"
