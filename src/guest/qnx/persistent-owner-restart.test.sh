#!/bin/bash
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
qnx_runtime=${2:-$root/src/guest/qnx/qnx-runtime.sh}
work=${VOYEMU_SUPERVISOR_TEST_TMPDIR:-$root/.local/build/tmp}
mkdir -p "$work"
tmp=$(mktemp -d "$work/qnx-owner-restart.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
awk '/^qm_ox\(\) \{/{copy=1} /^qm_bounded od |^VOYEMU_SUPERVISOR_CONFIG=ensure |^qm_sc\(\) \{/{if(copy)exit} copy{print}' "${1:-$root/src/guest/qnx/persistent-runtime.sh}" > "$tmp/helper.sh"
. "$tmp/helper.sh"
qm_render_preload='extension-a:extension-b:/proc/boot/bin/Snapdragon.rgb'
LD_LIBRARY_PATH='/lib64:/proc/boot/lib64:/proc/boot/lib64/dll:/lib:/usr/lib'
# Original ivcd default TCP mode listens on 80; dummy selects 6456.
# Execute the extracted launch argv against a stub, without starting a guest.
ivcd() { [ "$#" -eq 0 ]; }
ivcd_launch=$(sed -n '/^\/usr\/bin\/ivcd /{s@/usr/bin/ivcd@ivcd@;s@ 2>.*@@;p;}' "$qnx_runtime")
[ -n "$ivcd_launch" ]
eval "$ivcd_launch"
fixture() {
 local mode=$1 i owner flags n item
 echo '<clsboot_root>'
 echo '  <action_execs nums="26">'
 for ((i=0;i<26;i++)); do
  owner=unit$i
  case $i in 12) owner=light;; 13) owner=point;; 14) owner=gauge;; esac
  flags=
  if [ "$mode" = expected ] && [ "$i" -ge 12 ] && [ "$i" -le 14 ]; then
   flags=$(sed -n "/^QGFX_.* \/usr\/bin\/$owner -config=/s/ LD_PRELOAD=.*//p" "$qnx_runtime")
   [ -n "$flags" ]
  fi
  set -- $flags
  n=$((3+$#))
  [ -z "$flags" ] || n=$((n+1))
  echo '    <action_exec>'
  printf '      <m_args nums="2"><arg>/usr/bin/%s</arg><arg>-config=/usr/share/theme/application_%s.cfg</arg></m_args>\n' "$owner" "$owner"
  printf '      <m_envs nums="%s">\n' "$n"
  if [ -n "$flags" ]; then printf '      <env>LD_LIBRARY_PATH=%s</env>\n' "$LD_LIBRARY_PATH"; else echo '      <env>LD_LIBRARY_PATH=/proc/boot:/lib:/usr/lib:/lib/dll</env>'; fi
  echo '      <env>XDG_RUNTIME_DIR=/run/user/0</env>'
  echo '      <env>TMPDIR=/data</env>'
  for item in $flags; do printf '      <env>%s</env>\n' "$item"; done
  [ -z "$flags" ] || printf '      <env>LD_PRELOAD=%s</env>\n' "$qm_render_preload"
  echo '        <exp/>'
  echo '      </m_envs>'
  printf '      <m_readyFile>%s.pid</m_readyFile><m_monitorfailed_action>3</m_monitorfailed_action>\n' "$owner"
  echo '    </action_exec>'
 done
 echo '  </action_execs>'
 echo '</clsboot_root>'
}
fixture original > "$tmp/original.xml"
fixture expected > "$tmp/expected.xml"
if declare -F qm_ox > /dev/null; then
 qm_ox < "$tmp/original.xml" > "$tmp/actual.xml"
else
 # Previous runtime leaves native supervisor defaults unchanged.
 cp "$tmp/original.xml" "$tmp/actual.xml"
fi
cmp "$tmp/expected.xml" "$tmp/actual.xml"
# Mirror the original env parser: exp ends the native array, regardless of nums.
awk '/<m_envs nums=/{v=1} v&&/<exp\/>/{v=0} v&&/<env>LD_PRELOAD=/{n++} END{exit(n==3?0:1)}' "$tmp/actual.xml"
# Every non-owner byte, original argv, policies and unrelated env survive.
# Invalid input never yields an accepted configuration.
for mutation in missing duplicate preload qgfx count truncated root actionclose; do
 case $mutation in
 missing) sed 's@/usr/bin/gauge@/usr/bin/absent@' "$tmp/original.xml";;
 duplicate) sed 's@/usr/bin/point@/usr/bin/gauge@' "$tmp/original.xml";;
 preload) sed '/<arg>\/usr\/bin\/gauge/,/<\/action_exec>/s@<env>TMPDIR=/data</env>@<env>LD_PRELOAD=foreign</env>@' "$tmp/original.xml";;
 qgfx) sed '/<arg>\/usr\/bin\/gauge/,/<\/action_exec>/s@<env>TMPDIR=/data</env>@<env>QGFX_MCD_TRACE=0</env>@' "$tmp/original.xml";;
 count) sed 's@nums="3"@nums="4"@' "$tmp/original.xml";;
 root) sed '/^<clsboot_root>/d' "$tmp/original.xml";;
 actionclose) sed '/<\/action_exec>/d' "$tmp/original.xml";;
 truncated) sed '/<\/clsboot_root>/d' "$tmp/original.xml";;
 esac > "$tmp/bad.xml"
 if qm_ox < "$tmp/bad.xml" > "$tmp/rejected.xml"; then echo "accepted $mutation"; exit 1; fi
done
qm_render_preload='extension&amp:<path>:/proc/boot/bin/Snapdragon.rgb'
qm_ox < "$tmp/original.xml" > "$tmp/escaped.xml"
[ "$(grep -c 'LD_PRELOAD=extension&amp;amp:&lt;path&gt;:/proc/boot/bin/Snapdragon.rgb' "$tmp/escaped.xml")" -eq 3 ]

echo 'A9 native supervisor XML: exact initial adapter parity, nonowners, argv/policies, malformed/collision rejection and escaping PASS'

sed -n '/^qm_owner_pid() {/,/^}/p' "${1:-$root/src/guest/qnx/persistent-runtime.sh}" |
 sed "s|/log/run|$tmp|g" > "$tmp/owner.sh"
[ -s "$tmp/owner.sh" ]
. "$tmp/owner.sh"
function /bin/sleep {
 [ "$1" = 0.1 ]
 if [ "$mode" = delayed ]; then printf '%s\n' "$pid" > "$tmp/$owner.pid"; fi
}
function kill {
 [ "$#" -eq 2 ] && [ "$1" = -0 ] && [ "$2" = "$pid" ] && [ "$mode" != dead ]
}
pid=1245224
mode=ready
for owner in point light gauge; do
 pid=$((pid+1))
 printf '%s\n' "$pid" > "$tmp/$owner.pid"
 [ "$(qm_owner_pid "$owner")" = "$pid" ]
done
owner=light
for mode in delayed missing malformed dead; do
 rm -f "$tmp/$owner.pid"
 case "$mode" in
  malformed) printf '%s\n' 01 > "$tmp/$owner.pid";;
  dead) printf '%s\n' "$pid" > "$tmp/$owner.pid";;
 esac
 case "$mode" in
  delayed) [ "$(qm_owner_pid "$owner")" = "$pid" ];;
  *) if qm_owner_pid "$owner" > /dev/null 2>&1; then echo "admitted $mode owner"; exit 1; fi;;
 esac
done
echo 'A9 cold owner start: verified PID and bounded failure PASS'
