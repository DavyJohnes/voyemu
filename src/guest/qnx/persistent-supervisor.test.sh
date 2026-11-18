#!/bin/bash
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
script=${1:-$root/src/guest/qnx/persistent-runtime.sh}
qnx_runtime=${2:-$root/src/guest/qnx/qnx-runtime.sh}
owner_test=${3:-$root/src/guest/qnx/persistent-owner-restart.test.sh}
work=${VOYEMU_SUPERVISOR_TEST_TMPDIR:-$root/.local/build/tmp}
mkdir -p "$work"
tmp=$(mktemp -d "$work/qnx-supervisor.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
# Keep the existing native XML adapter tests, including its exact 26-entry fixture.
/bin/bash "$owner_test" "$script" "$qnx_runtime"
awk '/^qm_ox\(\) \{/{copy=1} /^qm_bounded od |^VOYEMU_SUPERVISOR_CONFIG=ensure |^qm_sc\(\) \{/{if(copy)exit} copy{print}' "$script" > "$tmp/helper.sh"
awk '/^qm_bounded od |^VOYEMU_SUPERVISOR_CONFIG=ensure |^qm_sc\(\) \{/{copy=1} copy{print}' "$script" |
 sed "s|/log/clsboot|$tmp/owned|g;s|/etc/clsboot|$tmp/etc|g;s|/bin/ls|qm_carrier|g;s|/bin/mkdir|qm_mkdir|g;s|/bin/ln|qm_ln|g;s|/bin/mv|qm_mv|g;s|/usr/bin/clusterboot|qm_clusterboot|g" > "$tmp/setup.sh"
[ -s "$tmp/helper.sh" ] && [ -s "$tmp/setup.sh" ]
. "$tmp/helper.sh"
qm_render_preload='extension-a:extension-b:/proc/boot/bin/Snapdragon.rgb'
LD_LIBRARY_PATH='/lib64:/proc/boot/lib64:/proc/boot/lib64/dll:/lib:/usr/lib'
{
 echo '<clsboot_root>'
 echo '  <action_execs nums="26">'
 for ((i=0;i<26;i++)); do
  owner=unit$i
  case $i in 12) owner=light;; 13) owner=point;; 14) owner=gauge;; esac
  echo '    <action_exec>'
  printf '      <m_args nums="2"><arg>/usr/bin/%s</arg><arg>-config=/usr/share/theme/application_%s.cfg</arg></m_args>\n' "$owner" "$owner"
  echo '      <m_envs nums="3">'
  echo '      <env>LD_LIBRARY_PATH=/proc/boot:/lib:/usr/lib:/lib/dll</env>'
  echo '      <env>XDG_RUNTIME_DIR=/run/user/0</env>'
  echo '      <env>TMPDIR=/data</env>'
  echo '        <exp/>'
  echo '      </m_envs>'
  printf '      <m_readyFile>%s.pid</m_readyFile><m_monitorfailed_action>3</m_monitorfailed_action>\n' "$owner"
  echo '    </action_exec>'
 done
 echo '  </action_execs>'
 echo '</clsboot_root>'
} > "$tmp/original.xml"
qm_ox < "$tmp/original.xml" > "$tmp/installed.xml"
printf '<!-- retained installed extension and transaction state -->\n' >> "$tmp/installed.xml"
cat > "$tmp/driver.sh" <<'DRIVER'
set -eu
. "$FIXTURE_DIR/helper.sh"
eval "$(declare -f qm_ox | sed '1s/qm_ox/qm_original_ox/')"
qm_ox() { echo qm_ox >> "$FIXTURE_DIR/calls"; qm_original_ox; }
owned_path() { case "$1" in "$FIXTURE_DIR"/*) ;; *) exit 125;; esac; }
qm_ensure_calls=0
qm_carrier() {
 if [ "${VOYEMU_SUPERVISOR_CONFIG-}" = abort ]; then
  echo abort >> "$FIXTURE_DIR/calls"
  [ ! -L "$FIXTURE_DIR/etc/boot_cfg.xml" ] || rm "$FIXTURE_DIR/etc/boot_cfg.xml"
  return 0
 fi
 [ "${VOYEMU_SUPERVISOR_CONFIG-}" = ensure ] || return 125
 [ "${LD_PRELOAD-}" = /proc/boot/bin/Snapdragon.rgb ] || return 125
 [ "$#" -eq 0 ] || return 125
 echo ensure >> "$FIXTURE_DIR/calls"
 qm_ensure_calls=$((qm_ensure_calls+1))
 if [ "$qm_ensure_calls" -eq 1 ]; then
  case $CASE in
  failed) return 1;; timeout|prepare-failed) return 125;;
  preserved|replay) return 0;;
  migrated) ;;
  *)
   [ ! -e "$FIXTURE_DIR/etc/boot_cfg.xml" ] && [ ! -L "$FIXTURE_DIR/etc/boot_cfg.xml" ] || return 125
   [ ! -e "$FIXTURE_DIR/owned/boot_cfg.xml" ] && [ ! -L "$FIXTURE_DIR/owned/boot_cfg.xml" ] || return 125
   mkdir -p "$FIXTURE_DIR/owned" || return 125
   ln -s "$FIXTURE_DIR/owned/boot_cfg.xml" "$FIXTURE_DIR/etc/boot_cfg.xml" || return 125
   return 2;;
  esac
 elif [ "$CASE" = migration-failed ]; then return 125
 fi
 [ -L "$FIXTURE_DIR/etc/boot_cfg.xml" ] || return 125
 cp "$FIXTURE_DIR/etc/boot_cfg.xml" "$FIXTURE_DIR/etc/migrated.xml" || return 125
 mv "$FIXTURE_DIR/etc/migrated.xml" "$FIXTURE_DIR/etc/boot_cfg.xml"
}
qm_mkdir() {
 echo mkdir >> "$FIXTURE_DIR/calls"
 [ "$#" -eq 2 ] && [ "$1" = -p ] || return 125; owned_path "$2"; mkdir -p "$2"
}
qm_ln() {
 echo ln >> "$FIXTURE_DIR/calls"
 [ "$#" -eq 3 ] && [ "$1" = -sfP ] || return 125; owned_path "$2"; owned_path "$3"; ln -sf "$2" "$3"
}
qm_clusterboot() {
 echo clusterboot >> "$FIXTURE_DIR/calls"
 [ "$#" -eq 2 ] && [ "$1" = -b ] && [ "$2" = gen ] || return 125
 case $CASE in
 transform-failed) echo '<broken>' > "$FIXTURE_DIR/etc/boot_cfg.xml";;
 *) cat "$FIXTURE_DIR/original.xml" > "$FIXTURE_DIR/etc/boot_cfg.xml";;
 esac
 case $CASE in
 generator-zero) return 0;; generator-failed) return 2;; generator-timeout) return 125;;
 esac
 return 1
}
qm_mv() {
 echo mv >> "$FIXTURE_DIR/calls"
 [ "$#" -eq 2 ] || return 125; owned_path "$1"; owned_path "$2"
 [ "$CASE" != publish-failed ] || return 125
 mv "$1" "$2"
}
qm_bounded() { shift; "$@"; }
# Guest startup does not use errexit; every admission error must terminate explicitly.
set +e
. "$FIXTURE_DIR/setup.sh"
echo services >> "$FIXTURE_DIR/calls"
DRIVER
run_case() {
 local scenario=$1 expected=$2 expected_rc=$3 rc=0
 : > "$tmp/calls"
 FIXTURE_DIR="$tmp" CASE="$scenario" qm_render_preload="$qm_render_preload" LD_LIBRARY_PATH="$LD_LIBRARY_PATH" /bin/bash "$tmp/driver.sh" > "$tmp/driver.log" 2>&1 || rc=$?
 printf '%s\n' $expected > "$tmp/expected-calls"
 if { [ "$expected_rc" = success ] && [ "$rc" -ne 0 ]; } || { [ "$expected_rc" = failure ] && [ "$rc" -eq 0 ]; } || ! cmp -s "$tmp/expected-calls" "$tmp/calls"; then
  echo "supervisor $scenario: expected $expected_rc with $expected; got rc=$rc"
  cat "$tmp/calls" "$tmp/driver.log"
  exit 1
 fi
}
for scenario in preserved migrated factory failed timeout prepare-failed generator-zero generator-failed generator-timeout transform-failed publish-failed migration-failed; do
 rm -rf "$tmp/etc" "$tmp/owned"
 mkdir -p "$tmp/etc" "$tmp/owned"
 case $scenario in
 preserved|failed|timeout) cp "$tmp/installed.xml" "$tmp/etc/boot_cfg.xml";;
 migrated) cp "$tmp/installed.xml" "$tmp/owned/boot_cfg.xml"; ln -s "$tmp/owned/boot_cfg.xml" "$tmp/etc/boot_cfg.xml";;
 esac
 case $scenario in
 preserved|migrated)
  run_case "$scenario" 'ensure services' success
  [ ! -L "$tmp/etc/boot_cfg.xml" ]
  cmp "$tmp/installed.xml" "$tmp/etc/boot_cfg.xml";;
 factory)
  run_case "$scenario" 'ensure clusterboot qm_ox mv ensure services' success
  [ ! -L "$tmp/etc/boot_cfg.xml" ]
  qm_ox < "$tmp/original.xml" > "$tmp/expected-factory.xml"
  cmp "$tmp/expected-factory.xml" "$tmp/etc/boot_cfg.xml"
  printf '<!-- retained installed extension and transaction state -->\n' >> "$tmp/etc/boot_cfg.xml"
  cp "$tmp/etc/boot_cfg.xml" "$tmp/replay.xml"
  run_case replay 'ensure services' success
  cmp "$tmp/replay.xml" "$tmp/etc/boot_cfg.xml";;
 failed|timeout)
  run_case "$scenario" 'ensure abort' failure
  cmp "$tmp/installed.xml" "$tmp/etc/boot_cfg.xml";;
 prepare-failed)
  run_case "$scenario" 'ensure abort' failure
  [ ! -e "$tmp/etc/boot_cfg.xml" ] && [ ! -L "$tmp/etc/boot_cfg.xml" ];;
 generator-zero|generator-failed|generator-timeout)
  run_case "$scenario" 'ensure clusterboot abort' failure;;
 transform-failed)
  run_case "$scenario" 'ensure clusterboot qm_ox abort' failure;;
 publish-failed)
  run_case "$scenario" 'ensure clusterboot qm_ox mv abort' failure;;
 migration-failed)
  run_case "$scenario" 'ensure clusterboot qm_ox mv ensure abort' failure;;
 esac
 [ ! -L "$tmp/etc/boot_cfg.xml" ] || { echo "temporary prefix retained after $scenario"; exit 1; }
done
echo 'A9 persistent supervisor: preserved/migrated byte identity, native factory/replay and fail-closed admission PASS'
