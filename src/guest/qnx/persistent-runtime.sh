/proc/boot/bin/devb-ram ram capacity=65536,address=0x86800000,nodinit blk devdir=/dev/ud,cache=512k qnx6 sync=optional &
qm_userdata_driver=$!
qm_userdata_wait=0
while [ "$qm_userdata_wait" -lt 100 ]; do
set -- /dev/ud/hd*
if [ "$#" -eq 1 ] && [ -b "$1" ]; then break; fi
kill -0 "$qm_userdata_driver" 2>/dev/null || { echo 'QSTORE FAIL userdata-driver'; exit 1; }
/bin/sleep 0.1; qm_userdata_wait=$((qm_userdata_wait+1))
done
[ "$#" -eq 1 ] && [ -b "$1" ] || { echo 'QSTORE FAIL userdata-device'; exit 1; }
qm_userdata_device=$1
VOYEMU_USERDATA_INITIALIZE=1 VOYEMU_USERDATA_DEVICE="$qm_userdata_device" LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /bin/ls
qm_userdata_initialize=$?
case "$qm_userdata_initialize" in
0) qm_bounded userdata-format /sbin/mkqnx6fs -q "$qm_userdata_device" || exit 1 ;;
1) ;;
*) echo 'QSTORE FAIL userdata-initialization'; exit 1 ;;
esac
qm_bounded userdata-mount /bin/mount -t qnx6 -o sync=optional "$qm_userdata_device" /var || exit 1
qm_bounded userdata-dirs /bin/mkdir -p /var/pps_persist /var/run /var/data || exit 1
qm_bounded userdata-data /bin/ln -sfP /var/data /data || exit 1
VOYEMU_STARTUP_EXTENSION=1 LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /bin/ls || { echo 'QSTORE FAIL startup-extension'; exit 1; }
qm_ancestor_preload_present=${LD_PRELOAD+x}
qm_ancestor_preload=${LD_PRELOAD-}
. /log/startup-extension.sh || { echo 'QSTORE FAIL startup-hook'; exit 1; }
qm_extension_preload=${LD_PRELOAD-}
qm_render_preload="${qm_extension_preload:+$qm_extension_preload:}/proc/boot/bin/Snapdragon.rgb"
qm_observer_preload="${qm_extension_preload:+$qm_extension_preload:}/proc/boot/bin/qditest"
if [ "$qm_ancestor_preload_present" = x ]; then
export LD_PRELOAD="$qm_ancestor_preload"
else
unset LD_PRELOAD
fi
echo 'QSTORE READY root=ro userdata=persistent'
qm_owner_pid() {
qm_file=/log/run/$1.pid
qm_i=0
while [ "$qm_i" -lt 100 ]; do
qm_pid=$([ ! -f "$qm_file" ] || /bin/cat "$qm_file")
case "$qm_pid" in
''|0*|1|*[!0-9]*|???????????*) ;;
*) if kill -0 "$qm_pid" 2>/dev/null; then echo "$qm_pid"; return 0; fi;;
esac
/bin/sleep 0.1
qm_i=$((qm_i+1))
done
echo "QIP FAIL graphics-owner=$1" >&2
return 1
}
qm_ox() {
/usr/bin/awk -v p="$qm_render_preload" -v l="$LD_LIBRARY_PATH" '
function x(s){gsub(/&/,"\\&amp;",s);gsub(/</,"\\&lt;",s);gsub(/>/,"\\&gt;",s);return s}
/<clsboot_root>/{r++}
/<action_exec>/{if(b)exit 1;b=1;u++;o=""}
/<arg>\/usr\/bin\/(gauge|point|light)<\/arg>/{
o=$0;sub(/^.*\/bin\//,"",o);sub(/<.*$/,"",o)
if(s[o]++)exit 1
f="MCD_TRACE=1 "
if(o=="light")f=f"MCD_OBSERVE=1"
else{
f=f"GL_BATCH=1 UNIFORM_PREFETCH=1 LANGUAGE_RELAY=1 THEME_RELAY=1"
if(o=="gauge")f="WHEEL_RELAY=0 MCU_RELAY=0 DLT_TAIL=1 THEME_REQUEST_ENTRY=1 "f
}
n=split(f,env)
}
o&&/<m_envs nums="3">/{
if(v)exit 1
sub(/nums="3"/,"nums=\"" n+4 "\"")
v=1;a=0;k=0
}
o&&v&&/<env>/{
a++
if(/<env>(LD_PRELOAD|QGFX_[^=]*)=/)exit 1
if(/<env>LD_LIBRARY_PATH=/){sub(/<env>.*$/,"");$0=$0 "<env>LD_LIBRARY_PATH=" x(l) "</env>";k++}
}
o&&v&&/<exp\/>/{
if(!v||a!=3||k!=1)exit 1
for(i=1;i<=n;i++)print"      <env>QGFX_" env[i] "</env>"
print"      <env>LD_PRELOAD=" x(p) "</env>"
v=0;d++
}
/<\/action_exec>/{if(!b||v)exit 1;b=0;o=""}
/<\/clsboot_root>/{z++}
{print}
END{if(r*z!=1||b||u!=26||d!=3)exit 1}
'
}
qm_sc() { VOYEMU_SUPERVISOR_CONFIG=$1 LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /bin/ls; }
qm_fail() { qm_sc abort;exit 1; }
qm_sc ensure
case $? in
0) ;;
2)
qm_bounded og /usr/bin/clusterboot -b gen
[ $? = 1 ]||qm_fail
qm_ox</log/clsboot/boot_cfg.xml>/log/clsboot/owner.xml||qm_fail
qm_bounded oc /bin/mv /log/clsboot/owner.xml /log/clsboot/boot_cfg.xml||qm_fail
qm_sc ensure||qm_fail;;
*) qm_fail;;
esac
