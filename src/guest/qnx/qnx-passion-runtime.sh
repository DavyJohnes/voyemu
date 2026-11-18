#!/proc/boot/bin/ksh
exec 1>&2
qm_source_capture=1
export PATH=/proc/boot/bin:/proc/boot:/bin:/usr/bin:/sbin
export LD_LIBRARY_PATH=/proc/boot/lib64:/proc/boot/lib64/dll:/lib64:/usr/lib64
qm_cmd() (
    [ "$#" -gt 0 ] && [ "$#" -le 16 ] || return 125
    for qm_av in 00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15; do unset VOYEMU_QBW_ARG_$qm_av; done
    export VOYEMU_QBW_ENABLE=1 VOYEMU_QBW_ARGC="$#"
    for key in 00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15; do
        [ "$#" -gt 0 ] || break
        export "VOYEMU_QBW_ARG_$key=$1"
        shift
    done
    export LD_PRELOAD=/proc/boot/bin/qditest
    exec /proc/boot/bin/ksh -c 'exit 125'
)
qm_wait() {
    count=0
    while [ ! -e "$1" ] && [ "$count" -lt 100 ]; do /bin/sleep 0.1; count=$((count+1)); done
    [ -e "$1" ]
}
# The mounted OEM filesystem is not the host IFS: a command name is measured, not assumed.
qm_pick() {
    for qm_p in "$@"; do [ -x "$qm_p" ] && { echo "$qm_p"; return 0; }; done
    return 1
}
# Diagnostic only: stream the whole slog2 world to the serial
# console so a watchdog record ('abnormal') appears BEFORE a reset can hide it in RAM;
# the ram64diag.so carrier interposer (refusing sysmgr_reboot) is the second channel.
qm_slog2w=$(qm_pick /proc/boot/bin/slog2info /bin/slog2info /usr/bin/slog2info) || qm_slog2w=
[ -n "$qm_slog2w" ] && { "$qm_slog2w" -w 2>&1 & echo "QGRESET slog2-watch pid=$!"; }
echo 'QPC BEGIN Passion source candidate; no guest/MCU acceptance implied'
qm_wait /proc/boot/bin/devb-ram || { echo 'QPC FAIL ram-driver'; exit 1; }
# capacity= is in 512-byte blocks; the root stays read-only, because a read-write root loses the mount.
# devb-ram address= is 32-bit (0x23c008000 becomes 0x3c008000), so hd0 goes through the pinned
# ram64 carrier (passion_ram64.h), which maps 0x7c008000 to the file system at guest-physical 0x27c008000.
VOYEMU_PASSION_RAM64=passion-20260403-2.1.4.80-system-ram64-v1 \
LD_PRELOAD=/proc/boot/etc/dispdump.sh \
/proc/boot/bin/devb-ram ram capacity=2462832,address=0x7c008000 blk devdir=/dev/oem,cache=512k,automount=hd0:/:qnx6:ro,after &
qm_ram=$!
# sys_monitor scans at ~boot+5 s with reboot:true for a missing iox-roudi, so the
# writable /var is mounted by this second devb-ram before the long compositor wait; iceoryx
# needs a world-writable /var/lock.
# A second automount of the OEM image (hd1) cannot hold /var: its LUN attach inherits the cam
# devsize of the first, so its superblock candidate {0x2000, devsize-1000} reads past the end and
# fails with E302 on every invocation form. /var is a sysram LUN instead, formatted by the guest
# mkqnx6fs, which writes both superblocks under the geometry cam reports.
/proc/boot/bin/devb-ram ram capacity=65536,nodinit blk devdir=/dev/qvx,memory=sysram,cache=512k &
qm_chmod0=$(qm_pick /proc/boot/bin/chmod /bin/chmod) || qm_chmod0=
qm_sleep0=$(qm_pick /proc/boot/bin/sleep /bin/sleep)
qm_mount0=$(qm_pick /sbin/mount /proc/boot/bin/mount /bin/mount) || qm_mount0=
qm_mkdir0=$(qm_pick /proc/boot/bin/mkdir /bin/mkdir) || qm_mkdir0=
qm_mkfs0=$(qm_pick /sbin/mkqnx6fs /proc/boot/bin/mkqnx6fs) || qm_mkfs0=
qm_vx=
qm_vt=0
while [ "$qm_vt" -lt 100 ] && [ -z "$qm_vx" ]; do
    for qm_vdev in /dev/qvx/hd*; do [ -e "$qm_vdev" ] && qm_vx="$qm_vdev" && break; done
    [ -z "$qm_vx" ] && "$qm_sleep0" 0.1 && qm_vt=$((qm_vt+1)) || qm_vt=100
done
echo "QPC VAR unit=${qm_vx:-none} mkfs=${qm_mkfs0:+set} mount=${qm_mount0:+set}"
if [ -n "$qm_vx" ] && [ -n "$qm_mkfs0" ] && [ -n "$qm_mount0" ]; then
    "$qm_mkfs0" -q "$qm_vx" && "$qm_mount0" -t qnx6 -o sync=optional "$qm_vx" /var && echo 'QPC VAR mount ok' || echo 'QPC VAR mount FAILED'
fi
qm_ticks0=0; while [ "$qm_ticks0" -lt 300 ] && [ ! -d /var/lock ]; do "$qm_sleep0" 0.1; qm_ticks0=$((qm_ticks0+1)); done
[ -d /var/lock ] || cp -R /var/run /var/lock 2>/dev/null
[ "$( (echo qw012 > /var/.qwprobe) && cat /var/.qwprobe 2>/dev/null)" = qw012 ] && echo 'QPC VARRW probe=y' || echo 'QPC VARRW probe=n'
# Printed before the roudi spawn, which a later reset cannot hide: the block device directories
# and the /var listing tell a stacked hd0 root from a failed /var mount.
echo "QPC VAR oem=$([ -e /dev/oem ] && echo y || echo n) qvx=$([ -e /dev/qvx ] && echo y || echo n) lock=$([ -d /var/lock ] && echo y || echo n) run=$([ -d /var/run ] && echo y || echo n) v=$(ls /var)"
qm_roudi=$(qm_pick /usr/bin/iox-roudi /proc/boot/bin/iox-roudi) || qm_roudi=
qm_on=$(qm_pick /bin/on /proc/boot/bin/on) || qm_on=
qm_rlib=/usr/lib64
"$qm_mkdir0" -p /var/lock /var/run /var/data /var/pps /var/pps_persist
"$qm_chmod0" 0777 /var/lock /var/run 2>/dev/null
echo "QPC DIAG varnew lock=$([ -d /var/lock ] && echo y || echo n)"
qm_e0=
if [ -n "$qm_roudi" ] && [ -d /var/lock ]; then
 if [ -n "$qm_on" ]; then
  LD_LIBRARY_PATH="$qm_rlib:$LD_LIBRARY_PATH" "$qm_on" -u 819:819 "$qm_roudi" -m off &
 else
  LD_LIBRARY_PATH="$qm_rlib:$LD_LIBRARY_PATH" "$qm_roudi" -m off &
 fi
 qm_e0=$!
 echo "QGRESET roudi-early pid=$qm_e0 bin=$qm_roudi route=on pre-scan"
else echo "QGRESET roudi-early none bin=${qm_roudi:-missing} varlock=no"; fi
# sys_monitor scans io-pkt-v4-hc (reboot:true, wait 5) at the same 5-s tick, so
# the real stack starts pre-scan with the OEM argv minus two VM-absent tokens:
# pkt_typed_mem=/ram/dma (no typed-mem here; sysram is the fallback) and
# `-d /lib64/libdevnp-emac-eth.so` (the VM has no HLOS_EMAC, and the emac driver dies with
# SIGSEGV in emac_config_parse before the scan).
# The driverless io-pkt is the real stack; -U restores the OEM thread uids; the scan
# needs the NAME, and root-run is the proven early-roudi pattern.
[ -x /bin/io-pkt-v4-hc ] && { /bin/io-pkt-v4-hc -S -v -D -t 2 -p tcpip stacksize=65536,mclbytes=4096,pagesize=65536,pkt_cache=1024,mbuf_cache=1024 -ppf-v4 -U 37:37,21,35,77,87,46,40 & echo QGRESET iopkt=$!; }
# A rejected preload kills the child, so the loop checks the parent, not the delay.
qm_ticks=0
while [ "$qm_ticks" -lt 600 ]; do
    [ -e /usr/bin/ivi_compositor ] && break
    [ -d "/proc/$qm_ram" ] || { echo "QPC DIAG ram-parent-exited ticks=$qm_ticks"; break; }
    /bin/sleep 0.1
    qm_ticks=$((qm_ticks+1))
done
echo "QPC DIAG ticks=$qm_ticks"
echo "QPC DIAG compositor=$([ -e /usr/bin/ivi_compositor ] && echo present || echo absent)"
echo "QPC DIAG binmount=$([ -e /bin/mount ] && echo yes || echo no)"
echo "QPC DIAG binls=$([ -e /bin/ls ] && echo yes || echo no)"
echo "QPC DIAG slm=$([ -e /slm ] && echo yes || echo no)"
# The ota RAM disk and its settle loop are deferred past the early roudi spawn:
# sys_monitor's first scan gives iox-roudi only 5 s grace (reboot:true in sys_monitor.json).
qm_wait /usr/bin/ivi_compositor || { echo 'QPC FAIL OEM-mount'; exit 1; }
echo 'QPC MOUNT system=readonly capacity=2462832'
export LD_LIBRARY_PATH=/proc/boot/lib64:/proc/boot/lib64/dll:/lib64:/usr/lib64:/opt/qt/lib
export PATH=/proc/boot/bin:/proc/boot:/bin:/usr/bin:/sbin
# Only the freshly created private RAM disk may be formatted; the hdN unit is a global counter.
for qm_unit in /dev/qpc/hd*; do
 [ -e "$qm_unit" ] && { echo 'QPC FAIL preexisting-private-disk'; exit 1; }
done
/proc/boot/bin/devb-ram ram capacity=131072,nodinit blk devdir=/dev/qpc,memory=sysram,cache=512k &
qm_priv=; qm_ticks3=0
while [ -z "$qm_priv" ] && [ "$qm_ticks3" -lt 300 ]; do
 for qm_unit in /dev/qpc/hd*; do
  [ -e "$qm_unit" ] && { qm_priv=$qm_unit; break; }
 done
 [ -n "$qm_priv" ] || { [ -n "$qm_sleep0" ] && "$qm_sleep0" 0.1; }
 qm_ticks3=$((qm_ticks3+1))
done
echo "QPC DIAG privatedisk=${qm_priv:-none} ticks=$qm_ticks3"
[ -n "$qm_priv" ] || { echo 'QPC FAIL private-disk'; exit 1; }
# What the mounted OEM root provides, measured rather than assumed.
echo "QPC DIAG logwrite=$([ -w /log ] && echo yes || echo no)"
qm_ln=$(qm_pick /proc/boot/bin/ln /sbin/ln) || qm_ln=
qm_cat=$(qm_pick /proc/boot/bin/cat /usr/bin/cat) || qm_cat=
qm_pidin=$(qm_pick /proc/boot/bin/pidin /sbin/pidin) || qm_pidin=
qm_slm=$(qm_pick /proc/boot/bin/slm /usr/bin/slm) || qm_slm=
qm_iopkt=$(qm_pick /proc/boot/bin/io-pkt-v6-hc /sbin/io-pkt-v6-hc) || qm_iopkt=
qm_ifconfig=$(qm_pick /sbin/ifconfig /proc/boot/bin/ifconfig) || qm_ifconfig=
echo "QPC DIAG tools mkfs=$qm_mkfs0 mount=$qm_mount0 mkdir=$qm_mkdir0 ln=$qm_ln cat=$qm_cat sleep=$qm_sleep0 pidin=$qm_pidin slm=$qm_slm iopkt=$qm_iopkt ifconfig=$qm_ifconfig roudi=$qm_roudi on=$qm_on chmod=$qm_chmod0"
[ -n "$qm_mkfs0" ] || { echo 'QPC FAIL no-mkqnx6fs'; exit 1; }
qm_cmd "$qm_mkfs0" -q "$qm_priv" || echo 'QPC PRIVATE mkqnx6fs failed'
if [ -n "$qm_mount0" ]; then qm_cmd "$qm_mount0" -t qnx6 -o sync=optional "$qm_priv" /log || echo 'QPC PRIVATE log-mount failed'; else echo 'QPC PRIVATE no-mount-tool'; fi
[ -n "$qm_mkdir0" ] && { qm_cmd "$qm_mkdir0" -p /log/data /log/tmp /log/ipc /log/run || echo 'QPC PRIVATE mkdir failed'; }
[ -n "$qm_ln" ] && { qm_cmd "$qm_ln" -sfP /log/data /data || true; qm_cmd "$qm_ln" -sfP /log/tmp /tmp || true; }
[ -n "$qm_pidin" ] && for qm_dp in $("$qm_pidin" ar 2>/dev/null | /usr/bin/awk '/[d]umper/{print $1}'); do kill $qm_dp 2>/dev/null; done
rm -f /tmp/*.core.gz
# The writable /var RAM disk and the early roudi spawn were moved before the OEM mount waits
# (sys_monitor's 5 s scan); their pre-scan diagnostics live near 'QPC BEGIN' above.
# Deferred ota disk: settle loop and mount follow the early spawn.
/proc/boot/bin/devb-ram ram capacity=2462832,address=0x27c008000 blk devdir=/dev/t32,cache=512k,automount=hd2:/ota:qnx6:ro,after &
qm_ticks2=0
while [ "$qm_ticks2" -lt 100 ]; do /bin/sleep 0.1; qm_ticks2=$((qm_ticks2+1)); done
echo "QPC DIAG t32ticks=$qm_ticks2"
qm_cmd /bin/mount
qm_cmd /bin/ls -l /dev/oem
# /slm/ic_qt.xml is the OEM's system file; /slm/ic_qt/ic_qt.xml is the component.
qm_icxml=/slm/ic_qt.xml
[ -e "$qm_icxml" ] || qm_icxml=/slm/ic_qt/ic_qt.xml
echo "QPC DIAG icxml=$qm_icxml logwrite=$([ -w /log ] && echo yes || echo no)"
echo "QPC DIAG startup_script size=$([ -e /scripts/startup.sh ] && /usr/bin/wc -c < /scripts/startup.sh || echo none)"
[ -n "$qm_iopkt" ] && { qm_cmd "$qm_iopkt" || echo 'QPC NETWORK spawn failed'; }
[ -n "$qm_ifconfig" ] && { qm_cmd "$qm_ifconfig" lo0 up || echo 'QPC NETWORK loopback failed'; }
if [ "${VOYEMU_PASSION_QNET-}" = 1 ] && [ -n "$qm_ifconfig" ]; then
 qm_cmd "$qm_ifconfig" tap0 create || exit 1
 qm_cmd "$qm_ifconfig" tap0 172.16.100.22 netmask 255.255.255.0 up || exit 1
 VOYEMU_QNET_RELAY=1 LD_PRELOAD=/bin/Snapdragon.rgb:secpol-preload.so /bin/ls -d / >&2 &
 echo "QPC QNET relay_pid=$! readiness=unverified"
fi
[ -n "$qm_ifconfig" ] && qm_cmd "$qm_ifconfig" -a > /log/interfaces.log 2>&1
[ -n "$qm_cat" ] && "$qm_cat" /log/interfaces.log
# setup_qt_env from scripts/startup.sh, with the OEM's own recorded physical screen size.
export LD_LIBRARY_PATH=/proc/boot/lib64:/proc/boot/lib64/dll:/lib64:/usr/lib64:/opt/qt/lib:/proc/boot/usr/lib64
export QT_PLUGIN_PATH=/opt/qt/plugins
export QML2_IMPORT_PATH=/opt/qt/qml
export FONTCONFIG_FILE=/opt/qt/lib/fontconfig/local.conf
export QQNX_PHYSICAL_SCREEN_SIZE=1920,720
export QT_LOGGING_TO_CONSOLE=1
echo "QPC DIAG devscreen=$([ -e /dev/screen ] && echo present || echo absent) devqvm=$([ -d /dev/qvm ] && echo yes || echo no) devgraphics=$(echo /dev/graphics/*) devgfx=$(echo /dev/screen/*)"
echo "QPC DIAG icbins ivi=$([ -x /usr/bin/ivi_compositor ] && echo yes || echo no) icservice=$([ -x /usr/bin/ic_service ] && echo yes || echo no) icbin=$(echo /usr/bin/ic_*)"
# The OEM boot starts a PPS server before any IC component.
qm_pps=$(qm_pick /bin/pps /proc/boot/bin/pps /usr/bin/pps) || qm_pps=
qm_ppsacl=$(qm_pick /etc/pps_acl.conf /proc/boot/etc/pps_acl.conf) || qm_ppsacl=
echo "QPC DIAG ppsbin=$qm_pps acl=$qm_ppsacl dir=$([ -d /var/pps ] && echo yes || echo no)"
if [ -n "$qm_pps" ]; then
 if [ -n "$qm_ppsacl" ]; then
  "$qm_pps" -A "$qm_ppsacl" -m /var/pps -p /var/pps_persist -t 100 > /log/pps.log 2>&1 &
 else
  "$qm_pps" -m /var/pps -p /var/pps_persist -t 100 > /log/pps.log 2>&1 &
 fi
 echo "QPC PPS spawned=$! readiness=unverified"
else echo 'QPC PPS no-pps'; fi
# OEM start_iceoryx ran 'iox-roudi -m off -c .../roudi_config.toml'.
qm_ioxlib=; for qm_il in /proc/boot/usr/lib64 /usr/lib64; do [ -z "$qm_ioxlib" ] && [ -d "$qm_il" ] && qm_ioxlib=$qm_il; done
qm_ioxcfg=; for qm_ic in /etc/iceoryx/roudi_config.toml /proc/boot/etc/iceoryx/roudi_config.toml; do
 [ -z "$qm_ioxcfg" ] && [ -e "$qm_ic" ] && qm_ioxcfg=$qm_ic
done
echo "QPC DIAG ioxlib=$qm_ioxlib members=$(echo $qm_ioxlib/libiceoryx*) cfg=$qm_ioxcfg"
qm_ioxt=/log/ioxr.toml
/usr/bin/awk 'BEGIN{print "[general]";print "version = 1";print "[[segment]]";split("128 2500 1024 1250 16384 250 131072 50 524288 13 1048576 8 4194304 3",a);for(i=1;i<15;i+=2){print "[[segment.mempool]]";print "size = "a[i];print "count = "a[i+1]}}' > $qm_ioxt
qm_ioxf=$qm_ioxt; [ -s "$qm_ioxf" ] || qm_ioxf=$qm_ioxcfg
qm_ioxo=; [ -n "$qm_ioxf" ] && qm_ioxo="-c $qm_ioxf"
echo "QPC IOXC cfg=$qm_ioxf opt=$qm_ioxo bytes=$(/proc/boot/bin/ls -l $qm_ioxt 2>&1 | /usr/bin/awk '{print $5}')"
[ -n "$qm_ioxcfg" ] && ( /usr/bin/tail -n 40 "$qm_ioxcfg" ) &
# The awk config is passed only when non-empty; -m off stays.
if [ -n "$qm_roudi" ] && { [ -z "${qm_e0-}" ] || { [ -n "$qm_pidin" ] && [ "$("$qm_pidin" ar 2>/dev/null | /usr/bin/awk '/iox-roudi/{print $1; exit}')" = "" ]; } }; then
 [ -n "$qm_mkdir0" ] && qm_cmd "$qm_mkdir0" -p /var/lock
 # As root the segment group is 'root' and ic_apps gets no writable segment, so the numeric 819:819 is used: 'on' rejects a name pair.
 qm_ioxr=on-iceoryx; qm_ioxl=/log/roudi-on.log; qm_seg=0; qm_ioxw=0
 if [ -n "$qm_on" ]; then
  echo 'QPC I1'
  LD_LIBRARY_PATH="$qm_ioxlib:$LD_LIBRARY_PATH" "$qm_on" -u 819:819 "$qm_roudi" -m off $qm_ioxo > $qm_ioxl 2>&1 &
  while [ "${qm_seg:-0}" -eq 0 ] && [ "$qm_ioxw" -lt 8 ]; do
   qm_ioxw=$((qm_ioxw+1)); [ -n "$qm_sleep0" ] && "$qm_sleep0" 3
   qm_seg=$(/usr/bin/awk '/Reserving/{n++} END{print n+0}' $qm_ioxl 2>/dev/null)
  done
  echo "QPC I2 route=$qm_ioxr wait=$qm_ioxw seg=$qm_seg"
 else echo 'QPC IOX no-on'; fi
 if [ "${qm_seg:-0}" -eq 0 ]; then
  echo "QPC IOX on-failed wait=$qm_ioxw"; /usr/bin/tail -n 20 $qm_ioxl
  qm_ioxr=direct; qm_ioxl=/log/roudi.log
  LD_LIBRARY_PATH="$qm_ioxlib:$LD_LIBRARY_PATH" "$qm_roudi" -m off $qm_ioxo > $qm_ioxl 2>&1 &
  [ -n "$qm_sleep0" ] && "$qm_sleep0" 8
 fi
 echo 'QPC I3'
 [ -n "$qm_chmod0" ] && "$qm_chmod0" 0666 /tmp/roudi /dev/shmem/iceoryx_mgmt /dev/shmem/root /dev/shmem/iceoryx 2>/dev/null
 qm_ioxn=$(/usr/bin/awk '/ready for clients/{n++} END{print n+0}' $qm_ioxl 2>/dev/null)
 # Publishers must sit in the segment's group: a 'root' segment means running them as root:root; only an
 # 'in the shared memory [name]' line carries a name, and a later line without one must not clear it.
 qm_segn=$(/usr/bin/awk '/in the shared memory/{n=$NF; gsub(/[][/]/,"",n); if (n!="iceoryx_mgmt") s=n} END{print s}' $qm_ioxl 2>/dev/null)
 [ "$qm_segn" = root ] && qm_icdir=1
 echo "QPC IQXSEG name=${qm_segn:-none} icdir=${qm_icdir:-0} route=$qm_ioxr wait=$qm_ioxw log=$qm_ioxl"
 echo "QPC IOX route=$qm_ioxr ready=$qm_ioxn seg=$qm_seg $(/proc/boot/bin/ls -l /tmp/roudi 2>&1)"
 /usr/bin/awk '/in the shared memory/{print "QPC SEG "$2" "$NF}' $qm_ioxl 2>/dev/null
else echo 'QPC IOX no-roudi'; fi
qm_smmu=$(qm_pick /proc/boot/bin/smmu_service /bin/smmu_service) || qm_smmu=
qm_kgsl=$(qm_pick /proc/boot/bin/kgsl /bin/kgsl /usr/bin/kgsl) || qm_kgsl=
qm_gsl=$(qm_pick /proc/boot/bin/gsl_hab_server /bin/gsl_hab_server /usr/bin/gsl_hab_server) || qm_gsl=
qm_screen=$(qm_pick /proc/boot/bin/screen /bin/screen /usr/bin/screen) || qm_screen=
qm_dispmgr=$(qm_pick /proc/boot/bin/disp_mgr /bin/disp_mgr /usr/bin/disp_mgr) || qm_dispmgr=
qm_owfd=$(qm_pick /proc/boot/bin/openwfd_server /bin/openwfd_server) || qm_owfd=
qm_wfdbe=$(qm_pick /proc/boot/bin/wfd_be /bin/wfd_be) || qm_wfdbe=
qm_bchip=$(qm_pick /proc/boot/bin/bridgechip_server /bin/bridgechip_server) || qm_bchip=
echo "QPC DIAG gfxbins smmu=$qm_smmu kgsl=$qm_kgsl gsl=$qm_gsl screen=$qm_screen dispmgr=$qm_dispmgr openwfd=$qm_owfd wfdbe=$qm_wfdbe bridge=$qm_bchip"
for qm_dc in /proc/boot/lib64/libdalconfig-sa8155_mega_mon_v11.0.33.so /lib64/libdalconfig-sa8155_mega_mon_v11.0.33.so /proc/boot/lib64/libdalconfig.so.1; do
 echo "QPC DIAG dalcand $qm_dc e=$([ -e "$qm_dc" ] && echo y || echo n)"
 [ -z "${qm_daltgt-}" ] && [ -e "$qm_dc" ] && qm_daltgt=$qm_dc
done
echo "QPC DIAG daltgt=${qm_daltgt-none}"
# graphics.conf names its khronos dlls bare; screen needs them beside the configuration.
qm_gd=/log/gfx
[ -n "$qm_mkdir0" ] && { qm_cmd "$qm_mkdir0" -p "$qm_gd" || echo 'QPC GFXDIR mkdir failed'; }
[ -n "$qm_cat" ] && "$qm_cat" /proc/boot/etc/graphics.conf > "$qm_gd/graphics.conf"
for qm_g in libESXEGL_Adreno.so libESXGLESv2_Adreno.so libscreen.so.1 libeglSubDriverQnx.so c2d30.so libopenwfd_qnx.so; do
 [ -n "$qm_ln" ] && { qm_cmd "$qm_ln" -sf "/proc/boot/lib64/$qm_g" "$qm_gd/$qm_g" || true; }
done
[ -n "${qm_daltgt-}" ] && [ -n "$qm_ln" ] && { qm_cmd "$qm_ln" -sf "$qm_daltgt" "$qm_gd/libdalconfig.so.1" || true; }
[ -n "$qm_smmu" ] && { LD_LIBRARY_PATH="$qm_gd:$LD_LIBRARY_PATH" LD_PRELOAD="${qm_daltgt-secpol-preload.so}" "$qm_smmu" -V 29:29,21 70:70,33,21 > /log/smmu.log 2>&1 & echo "QPC GFX smmu pid=$!"; }
[ -n "$qm_kgsl" ] && { "$qm_kgsl" > /log/kgsl.log 2>&1 & echo "QPC GFX kgsl pid=$!"; }
[ -n "$qm_gsl" ] && { "$qm_gsl" > /log/gsl.log 2>&1 & echo "QPC GFX gsl pid=$!"; }
[ -n "$qm_wfdbe" ] && { EARLY_VIDEO_ENABLE=ON MMLOG_CFG_PATH=/tmp/ LD_LIBRARY_PATH="$qm_gd:$LD_LIBRARY_PATH" LD_PRELOAD="${qm_daltgt-}" "$qm_wfdbe" > /log/wfdbe.log 2>&1 & echo "QPC GFX wfdbe pid=$!"; }
[ -n "$qm_bchip" ] && { LD_LIBRARY_PATH="$qm_gd:$LD_LIBRARY_PATH" LD_PRELOAD="${qm_daltgt-}" "$qm_bchip" > /log/bridgechip.log 2>&1 & echo "QPC GFX bridgechip pid=$!"; }
[ -n "$qm_dispmgr" ] && { LD_LIBRARY_PATH="$qm_gd:/lib64:/lib64/dll:/usr/lib64:/proc/boot/lib64:/proc/boot/lib64/dll" "$qm_dispmgr" > /log/dispmgr.log 2>&1 & echo "QPC GFX dispmgr pid=$!"; }
[ -n "$qm_screen" ] && { "$qm_screen" -c "$qm_gd/graphics.conf" > /log/screen.log 2>&1 & echo "QPC GFX screen pid=$!"; }
[ -n "$qm_owfd" ] && { DISPLAY_CATALOG_PATH=/proc/boot/bin LD_LIBRARY_PATH="$qm_gd:$LD_LIBRARY_PATH" LD_PRELOAD="${qm_daltgt-/lib64/libdalconfig.so.1}" "$qm_owfd" > /log/openwfd.log 2>&1 & echo "QPC GFX openwfd pid=$!"; }
[ -n "$qm_sleep0" ] && "$qm_sleep0" 4
echo "QPC DIAG devscreen2=$([ -e /dev/screen ] && echo present || echo absent) dev=$(echo /dev/*)"
for qm_lf in /log/smmu.log /log/kgsl.log /log/gsl.log /log/wfdbe.log /log/dispmgr.log /log/screen.log /log/openwfd.log /log/bridgechip.log /log/pps.log; do
 echo "QPC GFXLOG $qm_lf"; [ -n "$qm_cat" ] && "$qm_cat" "$qm_lf"
done
echo "QPC GFXCONF $qm_gd/graphics.conf"; [ -n "$qm_cat" ] && "$qm_cat" "$qm_gd/graphics.conf"
qm_slog2=$(qm_pick /proc/boot/bin/slog2info /bin/slog2info /usr/bin/slog2info) || qm_slog2=
qm_aps=$(qm_pick /bin/aps /proc/boot/bin/aps) || qm_aps=
if [ -n "$qm_aps" ]; then
 qm_cmd "$qm_aps" create -b 35 -B 300 aps_gvm; echo "QPC APS aps_gvm rc=$?"
 qm_cmd "$qm_aps" create -b 20 -B 100 aps_ic; echo "QPC APS aps_ic rc=$?"
else echo 'QPC APS no-aps'; fi
echo "QPC QTDIR $(/proc/boot/bin/ls -d /opt/qt/* 2>&1) data=$(/proc/boot/bin/ls -ld /data /data/qt 2>&1)"
# The plugin reads its layout from /opt/qt/config/screen_layout_config.json.
echo 'QPC QTCONF'; /proc/boot/bin/ls -l /opt/qt/config 2>&1; [ -f /opt/qt/config/screen_layout_config.json ] && /proc/boot/bin/cat /opt/qt/config/screen_layout_config.json
qm_spawn() { qm_pb=$1; qm_pt=$2; qm_pu=$3; shift 3
 [ -n "$qm_on" ] || { echo "QPC SPAWN no-on $qm_pb"; return 0; }
 "$qm_on" -T "$qm_pt" -u "$qm_pu" "$qm_pb" "$@" > /log/peer.log 2>&1 & echo "QPC SPAWN $qm_pb pid=$!"; }
qm_spawn park_assist ic_apps_t 834:819
qm_spawn mcu_hb mcu_hb_t mcu_hb 1000
[ -e /slm/msg_center.xml ] && { "$qm_slm" -V /slm/msg_center.xml > /log/msg.log 2>&1 & echo "QPC SPAWN msg_center pid=$!"; }
qm_svcxml=/slm/service_apps.xml
if [ -n "$qm_slm" ] && [ -e "$qm_svcxml" ]; then
 "$qm_slm" -V "$qm_svcxml" > /log/svc.log 2>&1 & echo "QPC SVC spawned=$! xml=$qm_svcxml"
 [ -n "$qm_sleep0" ] && "$qm_sleep0" 15
else echo 'QPC SVC no-file'; fi
if [ -n "$qm_slm" ]; then
 [ -n "$qm_cat" ] && { echo 'QPC ICXML'; "$qm_cat" "$qm_icxml"; "$qm_cat" "$qm_svcxml"; }
 # /slm/service_apps.xml has no ic_qt, so this is the compositor's only launch.
 [ -n "$qm_sleep0" ] && "$qm_sleep0" 30
 qm_wheel_tool=$(qm_pick /usr/bin/msg_center_test /bin/msg_center_test) || qm_wheel_tool=
 QGFX_PASSION_WHEEL_TOOL="$qm_wheel_tool" QGFX_WHEEL_RELAY=1 QGFX_PASSION_WHEEL_RELAY=1 QGFX_PRESENT_TRACE=1 LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb "$qm_slm" -V "$qm_icxml" > /log/slm.log 2>&1 & echo "QPC IC spawned=$! xml=$qm_icxml"
# 'showmem -s' reports sysram; '-t sysram' adds only noise.
qm_show=$(qm_pick /proc/boot/bin/showmem /bin/showmem /usr/bin/showmem) || qm_show=
echo "QPC MEM show=$qm_show"
qm_it=0
while [ "$qm_it" -lt 10 ]; do
 echo "QPC M t=$qm_it log=$(/bin/df -P /log 2>/dev/null | /usr/bin/awk 'NR==2{print $3"/"$2}') cores=$(/proc/boot/bin/ls /tmp/*.core.gz 2>/dev/null | /usr/bin/awk 'END{print NR+0}')"
 qm_it=$((qm_it+1)); [ -n "$qm_sleep0" ] && "$qm_sleep0" 3
done
 # ic_qt's stderr goes to /dev/slog2/stderr, which stays empty, so aborts are read from the guest RAM dump.
 # The harness starts its worker on the serial-log row 'QG3 READY', but 'slog2info -w > file' keeps the
 # ring on the guest disk, and its rows reach the log together, tens of seconds late, so stream 'QG3 ' to console.
 [ -n "$qm_slog2" ] && { "$qm_slog2" -w 2>&1 | /usr/bin/awk '/QG3 /{print}' & echo "QPC SLOGQ pid=$!"; }
 [ -n "$qm_slog2" ] && { "$qm_slog2" -w > /log/slogw.log 2>&1 & echo "QPC SLOGW pid=$!"; }
else echo 'QPC IC no-slm'; fi
# The show gate 'rootItem.opacity' (qmls/qnx_main.qml) has one source, 'icdriving/StartSelfCheck', behind five
# absent someipd-vlan1X (no EMAC): publishers are driven by hand.
qm_ipp=$(qm_pick /usr/bin/mega_ipc_pub /bin/mega_ipc_pub) || qm_ipp=
qm_mct=$(qm_pick /usr/bin/msg_center_test /bin/msg_center_test) || qm_mct=
# Each publisher stays detached: a tool blocked on a missing service must not stop the script.
# All instances register as the one name mega_ipc_pub-0, so parallel pubs unlink each other
# and only the last is delivered; a brief pause between pubs serialises the registrations.
qm_icq() { qm_qt=$1; qm_qu=$2; qm_qb=$3; shift 3
 [ -n "$qm_qb" ] || { echo "QPC IPC no-tool $qm_qt"; return 0; }
 if [ -n "$qm_icdir" ]; then "$qm_qb" "$@" >> /log/icpub.log 2>&1 &
 elif [ -n "$qm_on" ]; then "$qm_on" -T "$qm_qt" -u "$qm_qu" "$qm_qb" "$@" >> /log/icpub.log 2>&1 &
 else echo "QPC IPC no-on $qm_qt"; return 0; fi
 echo "QPC IPC pid=$! tool=$qm_qb args=$*"; [ -n "$qm_sleep0" ] && "$qm_sleep0" 1; }
echo "QPC IPC tools pub=$qm_ipp mct=$qm_mct ic_service=$([ -x /usr/bin/ic_service ] && echo yes || echo no)"
# The OEM's own key-injection daemon forwards steer keys (uartrpc_svc/steer_key, payload
# 6 bytes: 14 00 + 32-bit BE bitmap) to the compositor pid via screen_send_event; the wheel path
# of the cluster. OEM startup line: on -T input_service_t -u 815:815,68 /bin/input_service -p ivi_compositor.
if [ -x /bin/input_service ]; then
 # input_service_t/msg_center_t die in `on`'s secpol validator and a bare root run
 # gets UDS 'Permission denied'; the proven ic_apps_t gate accepts OEM tools (qm_icq pubs own
 # /tmp/mega_ipc_pub-0 fine), so the daemon rides the same type as the pubs.
 "$qm_on" -T ic_apps_t -u 834:819 /bin/input_service -p ivi_compositor > /log/input_service.log 2>&1 & echo "QPC SPAWN input_service pid=$!"
else echo 'QPC WHEEL no-input-service'; fi
[ -n "$qm_on" ] && [ -x /usr/bin/ic_service ] && { "$qm_on" -T ic_apps_t -u 834:819 /usr/bin/ic_service > /log/icsvc.log 2>&1 & echo "QPC ICSVC pid=$!"; }
echo 'QPC ID'; /usr/bin/awk '/iceoryx|ic_apps|msg_center|^root/{print FILENAME": "$0}' /etc/group /etc/passwd 2>&1
[ -n "$qm_slog2" ] && { "$qm_slog2" > /log/s0.log 2>&1; /usr/bin/awk '/QG3 READY/{print}' /log/s0.log; }
# Theme listing: settled offline (theme-hunt-20261104): no QML theme in the supply.
if [ -n "$qm_pidin" ]; then qm_cmd "$qm_pidin" ar > /log/processes.log 2>&1; [ -n "$qm_cat" ] && "$qm_cat" /log/processes.log; fi
[ -n "$qm_cat" ] && { echo 'QPC SVCINIT'; "$qm_cat" /log/svc.log; }
echo "QPC DIAG cores $(/proc/boot/bin/ls -l /tmp/*.core.gz 2>&1)"
rm -f /tmp/*.core.gz
# Probes write to a file and are catted: a pipeline forks twice.
# This public Screen operation reports actual output; it synthesizes no vehicle state.
QGFX_SOURCE_CAPTURE=1 QGFX_EXPORT_SCREEN=1 QGFX_SCREEN_STREAM=continuous LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /proc/boot/bin/ls -d /log
rc=$?
echo "QPC CAPTURE exit=$rc; IC/frame/MCU acceptance remains external"
qm_ldd=$(qm_pick /usr/bin/ldd /proc/boot/bin/ldd) || qm_ldd=
echo "QPC DIAG data=$(/proc/boot/bin/ls -ld /data 2>&1)"
# /data stays on the OEM's '/log/data'; the ALIVE tick carries the compositor pid.
qm_alive=0
while :; do
 [ -n "$qm_sleep0" ] && "$qm_sleep0" 30
 qm_alive=$((qm_alive+1))
 # Every tick: msg_center and the compositor both restart and drop earlier publications, which
 # leaves the frame black, so the gate and the parity topics re-publish while alive.
 qm_icq ic_apps_t 834:819 "$qm_ipp" -t icdriving/StartSelfCheck -m '{"value": 1}'
 # Parity topics: the cluster restarts its compositor per handover and must re-receive theme, language
 # and mileage-mode; DrivingInfo/Theme/Set also acks on DrivingInfo/Theme (QML publishes it retained).
 qm_icq ic_apps_t 834:819 "$qm_ipp" -t icdriving/UsageMode -m '{"value": 0}'
 qm_icq ic_apps_t 834:819 "$qm_ipp" -t DrivingInfo/Theme/Set -m '{"value": 1}'
 qm_icq ic_apps_t 834:819 "$qm_ipp" -t DrivingInfo/Language/Set -m '{"value": 2}'
 qm_icq ic_apps_t 834:819 "$qm_ipp" -t DrivingInfo/RemainingMileageMode/Set -m '{"value": 0}'
 [ "$qm_alive" -ge 3 ] && echo "QPC IXPOSH $(/usr/bin/awk '/NO_WRITABLE|no valid publisher port/{n++} END{print n+0" lines="NR}' /log/icpub.log 2>/dev/null)"
 [ "$qm_alive" = 2 ] && { echo 'QPC ICSVCLOG'; /usr/bin/tail -n 20 /log/icsvc.log 2>&1; echo 'QPC IPCLOG'; /usr/bin/tail -n 25 /log/icpub.log 2>&1; }
  # RouDi's own tail is printed every tick: it dates its death and names the exception.
 echo 'QPC RDLOG'; /usr/bin/tail -n 8 $qm_ioxl 2>&1
 rm -f /tmp/*.core.gz
 # The bare mask is safe: the ALIVE filter below matches the full path, not this rm line.
 echo "QPC ALIVE ticks=$qm_alive compositor=$([ -n "$qm_pidin" ] && "$qm_pidin" ar 2>/dev/null | /usr/bin/awk '/\/usr\/bin\/ivi_compositor/{print $1; exit}')"
 echo "QPC DEVS screen=$([ -e /dev/screen ] && echo present || echo absent)"
 [ -n "$qm_pidin" ] && "$qm_pidin" 2>/dev/null | /usr/bin/awk '/\/usr\/bin\/ivi_compositor/{print}'
 echo 'QPC SLMTAIL'
 /usr/bin/tail -n 30 /log/slm.log
 echo 'QPC ICQT'; /usr/bin/awk '!/distance-field glyph/{print}' /log/slogw.log 2>/dev/null | /usr/bin/tail -n 12
 [ -n "$qm_slog2" ] && { "$qm_slog2" > /log/s2.log 2>&1; /usr/bin/awk '/QG3 READY/{print}' /log/s2.log; /usr/bin/awk '/ivi_compositor/{print}' /log/s2.log > /log/ic2.log; /usr/bin/tail -n 10 /log/ic2.log; }
 [ -n "$qm_cat" ] && { echo 'QPC SVCTAIL'; /usr/bin/tail -n 6 /log/svc.log; }
 [ -n "$qm_chmod0" ] && "$qm_chmod0" 0666 /tmp/roudi /dev/shmem/iceoryx_mgmt /dev/shmem/root /dev/shmem/iceoryx 2>/dev/null
 echo 'QPC PEERTAIL'; /usr/bin/tail -n 3 /log/peer.log 2>&1; /usr/bin/tail -n 3 /log/msg.log 2>&1
 /usr/bin/tail -n 4 /log/roudi.log
done
