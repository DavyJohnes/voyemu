#!/bin/bash
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
work=${VOYEMU_SUPERVISOR_TEST_TMPDIR:-$root/.local/build/tmp}
mkdir -p "$work"
tmp=$(mktemp -d "$work/qnx-preload-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
sed -n '/^VOYEMU_STARTUP_EXTENSION=/,/^qm_ox() {/p' "${1:-$root/src/guest/qnx/persistent-runtime.sh}" | sed '$d' > "$tmp/stage.sh"
# Redirect only the guest hook path; every other statement is the production excerpt.
sed "s|/log/startup-extension.sh|$tmp/hook.sh|g" "$tmp/stage.sh" > "$tmp/source.sh"
function /bin/ls { [ "$LD_PRELOAD" = /proc/boot/bin/Snapdragon.rgb ] && return "$implant_rc"; }
check_ambient() {
    /bin/bash -c '[ "${LD_PRELOAD+x}" = "$1" ] && [ "${LD_PRELOAD-}" = "$2" ]' preload-child "$ancestor_present" "$ancestor" || { echo "child preload leaked ($mode/$hook)"; exit 1; }
    [ "${LD_PRELOAD+x}" = "$ancestor_present" ] && [ "${LD_PRELOAD-}" = "$ancestor" ] || { echo "ambient preload leaked ($mode/$hook)"; exit 1; }
}
for mode in unset empty inherited; do
    for hook in append empty unset; do
        case $hook in
            append) echo 'export LD_PRELOAD="${LD_PRELOAD:+$LD_PRELOAD:}extension-a:extension-b"' ;;
            empty) echo 'export LD_PRELOAD=' ;;
            unset) echo 'unset LD_PRELOAD' ;;
        esac > "$tmp/hook.sh"
        (
            ancestor= ancestor_present=
            case $mode in
                unset) unset LD_PRELOAD ;;
                empty) export LD_PRELOAD=; ancestor_present=x ;;
                inherited) ancestor='user-a:user-b'; ancestor_present=x; export LD_PRELOAD="$ancestor" ;;
            esac
            implant_rc=0
            . "$tmp/source.sh"
            check_ambient
            expected=
            if [ "$hook" = append ]; then expected="${ancestor:+$ancestor:}extension-a:extension-b"; fi
            [ "$qm_extension_preload" = "$expected" ]
            [ "$qm_render_preload" = "${expected:+$expected:}/proc/boot/bin/Snapdragon.rgb" ]
            [ "$qm_observer_preload" = "${expected:+$expected:}/proc/boot/bin/qditest" ]
            LD_PRELOAD="$qm_render_preload" /bin/bash -c '[ "$LD_PRELOAD" = "$1" ]' renderer-child "$qm_render_preload"
            LD_PRELOAD="$qm_observer_preload" /bin/bash -c '[ "$LD_PRELOAD" = "$1" ]' observer-child "$qm_observer_preload"
            function renderer { [ "$LD_PRELOAD" = "$qm_render_preload" ] || return 99; return 17; }
            function observer { [ "$LD_PRELOAD" = "$qm_observer_preload" ]; }
            # Implant failures must remain observable to the caller.
            rc=0; LD_PRELOAD="$qm_render_preload" renderer || rc=$?
            [ "$rc" -eq 17 ]
            LD_PRELOAD="$qm_observer_preload" observer
            check_ambient
        )
    done
done
echo 'return 0' > "$tmp/hook.sh"
rc=0; (implant_rc=23; . "$tmp/source.sh"; exit 99) || rc=$?
[ "$rc" -eq 1 ]
echo 'return 24' > "$tmp/hook.sh"
rc=0; (implant_rc=0; . "$tmp/source.sh"; exit 99) || rc=$?
[ "$rc" -eq 1 ]
echo 'persistent preload isolation: 9 environment cases and failure gates passed'
