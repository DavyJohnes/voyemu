#!/proc/boot/bin/ksh
exec 1>&2
qm_files() {
    for qm_path in /dev/hd0 /usr/bin/gauge /usr/share/theme/application_gauge.cfg /lib64/libkzcoreui.so; do
        echo "QMC CHECK file=$qm_path"
        if [ -r "$qm_path" ]; then echo "QMC FILE present=$qm_path"; else echo "QMC FILE absent=$qm_path"; fi
    done
}
case "${1-}" in
    --voyemu-files-only)
        [ "$#" -eq 1 ] || exit 125
        qm_files
        exit $?
        ;;
    --voyemu-files-*) exit 125 ;;
esac
qm_source_capture=0
export PATH=/bin:/proc/boot/bin:/proc/boot:/usr/bin
export LD_LIBRARY_PATH=/lib64:/proc/boot/lib64:/proc/boot/lib64/dll:/lib:/usr/lib
qm_native_external() (
    # Scope transport to this initialized carrier; child env is sanitized in C.
    [ "$#" -gt 0 ] && [ "$#" -le 16 ] || return 125
    unset VOYEMU_QBW_ARG_00 VOYEMU_QBW_ARG_01 VOYEMU_QBW_ARG_02 VOYEMU_QBW_ARG_03 VOYEMU_QBW_ARG_04 VOYEMU_QBW_ARG_05 VOYEMU_QBW_ARG_06 VOYEMU_QBW_ARG_07 VOYEMU_QBW_ARG_08 VOYEMU_QBW_ARG_09 VOYEMU_QBW_ARG_10 VOYEMU_QBW_ARG_11 VOYEMU_QBW_ARG_12 VOYEMU_QBW_ARG_13 VOYEMU_QBW_ARG_14 VOYEMU_QBW_ARG_15
    export VOYEMU_QBW_ENABLE=1 VOYEMU_QBW_ARGC="$#"
    for qm_native_key in 00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15; do
        [ "$#" -gt 0 ] || break
        export "VOYEMU_QBW_ARG_$qm_native_key=$1"
        shift
    done
    export LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb
    exec /proc/boot/bin/ksh -c 'exit 125'
)
qm_bounded() {
    qm_tag=$1; shift
    echo "QMC CHILD begin=$qm_tag"
    if [ "${VOYEMU_QBW_EXTERNAL+x}" = x ]; then
        if [ "$VOYEMU_QBW_EXTERNAL" != 1 ]; then
            echo "QMC CHILD exit=$qm_tag code=125"
            return 125
        fi
        case "$1" in /*)
            qm_native_external "$@"
            qm_rc=$?
            echo "QMC CHILD exit=$qm_tag code=$qm_rc"
            return "$qm_rc"
        esac
    fi
    "$@" &
    qm_pid=$!; qm_count=0; qm_fast=0; qm_timeout=0
    while kill -0 "$qm_pid" 2>/dev/null && [ "$qm_count" -lt 400 ]; do
        if [ "$qm_fast" -lt 10 ]; then
            /bin/sleep 0.01
            qm_fast=$((qm_fast + 1))
        else
            /bin/sleep 0.1
            qm_count=$((qm_count + 1))
        fi
    done
    if kill -0 "$qm_pid" 2>/dev/null; then
        echo "QMC CHILD timeout=$qm_tag"
        qm_timeout=1
        kill "$qm_pid" 2>/dev/null
        /bin/sleep 0.1
        kill -KILL "$qm_pid" 2>/dev/null
    fi
    wait "$qm_pid"
    qm_rc=$?
    if [ "$qm_timeout" -eq 1 ]; then qm_rc=124; fi
    echo "QMC CHILD exit=$qm_tag code=$qm_rc"
    return "$qm_rc"
}
qm_driver_native_delay() (
    [ "$#" -eq 0 ] || return 125
    unset VOYEMU_QBW_ARG_00 VOYEMU_QBW_ARG_01 VOYEMU_QBW_ARG_02 VOYEMU_QBW_ARG_03 VOYEMU_QBW_ARG_04 VOYEMU_QBW_ARG_05 VOYEMU_QBW_ARG_06 VOYEMU_QBW_ARG_07 VOYEMU_QBW_ARG_08 VOYEMU_QBW_ARG_09 VOYEMU_QBW_ARG_10 VOYEMU_QBW_ARG_11 VOYEMU_QBW_ARG_12 VOYEMU_QBW_ARG_13 VOYEMU_QBW_ARG_14 VOYEMU_QBW_ARG_15
    export VOYEMU_QBW_ENABLE=driver-delay100ms-v1 VOYEMU_QBW_ARGC=0
    export LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb
    exec /proc/boot/bin/ksh -c 'exit 125'
)
qm_initial_native_delay() (
    [ "$#" -eq 0 ] || return 125
    unset VOYEMU_QBW_ARG_00 VOYEMU_QBW_ARG_01 VOYEMU_QBW_ARG_02 VOYEMU_QBW_ARG_03 VOYEMU_QBW_ARG_04 VOYEMU_QBW_ARG_05 VOYEMU_QBW_ARG_06 VOYEMU_QBW_ARG_07 VOYEMU_QBW_ARG_08 VOYEMU_QBW_ARG_09 VOYEMU_QBW_ARG_10 VOYEMU_QBW_ARG_11 VOYEMU_QBW_ARG_12 VOYEMU_QBW_ARG_13 VOYEMU_QBW_ARG_14 VOYEMU_QBW_ARG_15
    export VOYEMU_QBW_ENABLE=initial-delay10ms-v1 VOYEMU_QBW_ARGC=0
    export LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb
    exec /proc/boot/bin/ksh -c 'exit 125'
)
qm_wait_ram_driver() {
    case "${VOYEMU_QNX_DRIVER_NATIVE_DELAY-}" in
        ''|1) ;;
        *) echo 'QMC FAIL RAM-delay-mode'; return 125 ;;
    esac
    qm_driver_attempt=0
    while [ "$qm_driver_attempt" -lt 160 ]; do
        if [ -x /proc/boot/bin/devb-ram ]; then
            echo 'QMC RAM driver-ready=1'
            return 0
        fi
        if [ "${VOYEMU_QNX_DRIVER_NATIVE_DELAY-}" = 1 ]; then
            if ! qm_driver_native_delay; then echo 'QMC FAIL RAM-native-delay'; return 125; fi
        else
            /bin/sleep 0.1
        fi
        qm_driver_attempt=$((qm_driver_attempt+1))
    done
    qm_driver_r=0; qm_driver_x=0
    if [ -r /proc/boot/bin/devb-ram ]; then qm_driver_r=1; fi
    if [ -x /proc/boot/bin/devb-ram ]; then qm_driver_x=1; fi
    echo "QMC RAM path=/proc/boot/bin/devb-ram readable=$qm_driver_r executable=$qm_driver_x"
    echo 'QMC FAIL RAM-driver-timeout'
    return 1
}
qm_wait_oem_mount() {
    qm_mount_attempt=0
    while [ "$qm_mount_attempt" -lt 160 ]; do
        if ! kill -0 "$qm_ram" 2>/dev/null; then echo 'QMC FAIL RAM-parent-exited-before-mount'; return 1; fi
        if [ -r /usr/bin/gauge ] && [ -r /usr/share/theme/application_gauge.cfg ] && [ -r /lib64/libkzcoreui.so ]; then
            echo 'QMC RAM files-ready=1'
            return 0
        fi
        /bin/sleep 0.1
        qm_mount_attempt=$((qm_mount_attempt+1))
    done
    echo 'QMC FAIL RAM-mount-timeout'
    return 1
}
qm_wait_pps() {
    qm_pps_attempt=0
    while [ "$qm_pps_attempt" -lt 160 ]; do
        if ! kill -0 "$qm_pps" 2>/dev/null; then echo 'QIP FAIL PPS-parent-exited'; return 1; fi
        if [ -r /var/pps/.all ]; then
            if ! { echo 'ready::1' > /var/pps/voyemu_startup_ready; } 2>/log/pps-write-error.log; then
                /bin/cat /log/pps-write-error.log | while IFS= read -r line; do echo "QIP PPS error=$line"; done
                echo 'QIP FAIL PPS-write'; return 1
            fi
            qm_pps_reply=$(/bin/dd if=/var/pps/voyemu_startup_ready bs=1024 count=1 2>/log/pps-read-error.log)
            qm_pps_read_rc=$?
            if [ "$qm_pps_read_rc" -ne 0 ]; then
                echo "QIP FAIL PPS-read code=$qm_pps_read_rc reply=$qm_pps_reply"
                /bin/cat /log/pps-read-error.log | while IFS= read -r line; do echo "QIP PPS error=$line"; done
                return 1
            fi
            if echo "$qm_pps_reply" | /bin/grep -q '^@voyemu_startup_ready$' && echo "$qm_pps_reply" | /bin/grep -q '^ready::1$'; then
                if ! kill -0 "$qm_pps" 2>/dev/null; then echo 'QIP FAIL PPS-parent-exited'; return 1; fi
                echo 'QIP PPS service-ready=1'
                return 0
            fi
        fi
        /bin/sleep 0.1
        qm_pps_attempt=$((qm_pps_attempt+1))
    done
    echo 'QIP FAIL PPS-readiness-timeout'
    return 1
}
qm_wait_dlt() {
    qm_dlt_attempt=0
    while [ "$qm_dlt_attempt" -lt 160 ]; do
        if ! kill -0 "$qm_dlt" 2>/dev/null; then echo 'QIP FAIL DLT-parent-exited'; return 1; fi
        if [ -S /tmp/dlt-ctrl.sock ]; then
            echo 'QIP DLT control-socket-ready=1'
            return 0
        fi
        /bin/sleep 0.1
        qm_dlt_attempt=$((qm_dlt_attempt+1))
    done
    echo 'QIP FAIL DLT-readiness-timeout'
    return 1
}
qm_mcd_predicate() {
    qm_core_mark="$1:$2:$3"
    if { [ "$qm_core_mark" != "$qm_core_last" ] && [ "$qm_core_reports" -lt 8 ]; } || [ "${4-}" = final ]; then
        echo "QIP IPC predicate=mcd pid=$qm_core_pid owner_alive=$1 socket_S=$2 pidfile_s=$3 attempt=$qm_core_seen_attempt phase=${4-snapshot}"
        qm_core_last=$qm_core_mark
        qm_core_reports=$((qm_core_reports+1))
    fi
}
qm_wait_core_ipc() {
    qm_core_pid=$1; qm_core_socket=$2; qm_core_pidfile=$3; qm_core_tag=$4
    qm_core_attempt=0; qm_core_seen_attempt=0; qm_core_diag=0; qm_core_last=; qm_core_reports=0
    if [ "$qm_core_tag" = mcd ]; then
        case "${VOYEMU_QNX_MCD_DIAGNOSTIC-}" in
            '') ;;
            1) qm_core_diag=1 ;;
            *) echo 'QIP FAIL IPC-diagnostic-mode=mcd'; return 125 ;;
        esac
    fi
    while [ "$qm_core_attempt" -lt 160 ]; do
        qm_core_seen_attempt=$qm_core_attempt
        if ! kill -0 "$qm_core_pid" 2>/dev/null; then
            if [ "$qm_core_diag" = 1 ]; then qm_mcd_predicate 0 not_tested not_tested final; fi
            echo "QIP FAIL IPC-parent-exited=$qm_core_tag"; return 1
        fi
        if [ "$qm_core_diag" = 1 ]; then
            # Preserve the original short circuit: pidfile_s is unobserved when socket_S is false.
            qm_core_s=0; qm_core_p=not_tested
            if [ -S "$qm_core_socket" ]; then
                qm_core_s=1; qm_core_p=0
                if [ -s "$qm_core_pidfile" ]; then qm_core_p=1; fi
            fi
            qm_mcd_predicate 1 "$qm_core_s" "$qm_core_p"
            if [ "$qm_core_s:$qm_core_p" = 1:1 ]; then
                echo "QIP IPC service-ready=$qm_core_tag"; return 0
            fi
        elif [ -S "$qm_core_socket" ] && [ -s "$qm_core_pidfile" ]; then
            echo "QIP IPC service-ready=$qm_core_tag"
            return 0
        fi
        /bin/sleep 0.1
        qm_core_attempt=$((qm_core_attempt+1))
    done
    if [ "$qm_core_diag" = 1 ]; then qm_mcd_predicate 1 "$qm_core_s" "$qm_core_p" final; fi
    echo "QIP FAIL IPC-readiness-timeout=$qm_core_tag"
    return 1
}
qm_wait_window() {
    qm_ready_attempt=0
    while [ "$qm_ready_attempt" -lt "$3" ]; do
        if ! kill -0 "$1" 2>/dev/null; then echo "QIP $2 exited-before-window"; return 1; fi
        for qm_ready_window in /dev/screen/"$1"/win-*; do
            if [ -e "$qm_ready_window" ]; then echo "QIP $2 window-ready=$qm_ready_window"; return 0; fi
        done
        /bin/sleep 0.5
        qm_ready_attempt=$((qm_ready_attempt+1))
    done
    echo "QIP $2 window-timeout"
    return 1
}
qm_export_screen() {
    qm_export_attempt=0
    while [ "$qm_export_attempt" -lt 60 ]; do
        echo "QC1 SCREENSHOT attempt=$qm_export_attempt"
        if [ "$qm_source_capture" = 1 ]; then
            QGFX_SOURCE_CAPTURE=1 QGFX_EXPORT_SCREEN=1 QGFX_SCREEN_STREAM=continuous LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /bin/ls -d /log
        else
        QGFX_EXPORT_SCREEN=1 QGFX_SCREEN_STREAM=continuous LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /proc/boot/bin/openwfd_telltale -size=1920x720 -file=/log/oem-screen.bmp
        fi
        qc_status=$?
        if [ "$qc_status" -eq 0 ] && [ -s /log/oem-screen.bmp ]; then echo 'QC1 STREAM completed'; return 0; fi
        /bin/sleep 1
        qm_export_attempt=$((qm_export_attempt+1))
    done
    echo 'QC1 FAIL screen-export-timeout'
    return 1
}
qm_observer_ram_check() {
    qm_bounded observer-padded-copy /bin/dd if=/proc/boot/bin/qditest of=/log/mcd-observer-padded.so bs=16928 count=1 || return 1
    qm_observer_hash=$(/bin/openssl dgst -sha256 /log/mcd-observer-padded.so)
    case "$qm_observer_hash" in
        *2e13e9b8299f882414400969309c501c639314a92f5d0e603806e0f13d37426c) echo 'QIP OBSERVER RAM-padded-hash-verified' ;;
        *) echo 'QIP FAIL observer-RAM-padded-hash'; return 1 ;;
    esac
    /bin/chmod 0755 /log/mcd-observer-padded.so || return 1
    QGFX_MCD_TRACE=0 QGFX_MCD_OBSERVE=0 LD_PRELOAD=/log/mcd-observer-padded.so /bin/ls -ld /log > /log/observer-ram-padded-probe.log 2>&1
    qm_probe_rc=$?
    /bin/cat /log/observer-ram-padded-probe.log
    echo "QIP OBSERVER RAM-padded-loader-code=$qm_probe_rc"
}
echo 'QMC BEGIN raw-OEM-QNX6-readonly'
echo 'QMC WAIT initial_sleep before'
case "${VOYEMU_QNX_INITIAL_NATIVE_DELAY-}" in
    '') if ! /bin/sleep 0.01; then echo 'QMC FAIL fractional-sleep-required'; exit 1; fi ;;
    1) if ! qm_initial_native_delay; then echo 'QMC FAIL initial-native-delay'; exit 125; fi ;;
    *) echo 'QMC FAIL initial-delay-mode'; exit 125 ;;
esac
echo 'QMC WAIT initial_sleep after'
# devb-ram arrives with ifs_disk after this display script starts.
echo 'QMC WAIT ram_driver before'
qm_wait_ram_driver || exit 1
echo 'QMC WAIT ram_driver after'
echo 'QMC RAM begin raw-system-offset=32768 readonly'
/proc/boot/bin/devb-ram ram capacity=2097152,address=0xC0008000 blk devdir=/dev/oem,cache=512k,automount=hd0:/:qnx6:ro,after &
qm_ram=$!
qm_wait_oem_mount || exit 1
if kill -0 "$qm_ram" 2>/dev/null; then echo "QMC RAM parent-alive=$qm_ram"; else wait "$qm_ram"; echo "QMC RAM parent-exit=$?"; fi
if [ "${VOYEMU_QBW_EXTERNAL-}" = 1 ]; then
    qm_bounded files /proc/boot/bin/ksh /proc/boot/bin/ssplash --voyemu-files-only
else
    qm_bounded files qm_files
fi
export LD_LIBRARY_PATH=/lib64:/usr/lib:/lib64:/proc/boot/lib64:/proc/boot/lib64/dll:/lib:/usr/lib
export PATH=/bin:/usr/bin:/bin:/proc/boot/bin:/proc/boot
qm_bounded config /bin/cat /usr/share/theme/application_gauge.cfg || exit 1
qm_bounded hash /bin/openssl dgst -sha256 /usr/bin/gauge || exit 1
echo 'QIP BEGIN owned-RAM-disk-and-OEM-services'
if [ -e /dev/qip/hd1 ]; then echo 'QIP FAIL unexpected-existing-disk'; exit 1; fi
/proc/boot/bin/devb-ram ram capacity=65536,nodinit blk devdir=/dev/qip,memory=sysram,cache=512k &
qm_logram=$!
qm_disk_wait=0
while [ ! -e /dev/qip/hd1 ] && [ "$qm_disk_wait" -lt 10 ]; do /bin/sleep 1; qm_disk_wait=$((qm_disk_wait+1)); done
qm_bounded devnodes /bin/ls -l /dev/qip /dev/ram0 /dev/hd0 /dev/socket
if kill -0 "$qm_logram" 2>/dev/null; then echo "QIP RAM parent-alive=$qm_logram"; else wait "$qm_logram"; echo "QIP RAM parent-exit=$?"; fi
if [ ! -e /dev/qip/hd1 ]; then echo 'QIP FAIL owned-ram-disk'; exit 1; fi
# Only this freshly created private 32 MiB RAM disk may be formatted.
qm_bounded logformat /sbin/mkqnx6fs -q /dev/qip/hd1 || exit 1
qm_bounded logmount /bin/mount -t qnx6 -o sync=optional /dev/qip/hd1 /log || exit 1
qm_bounded logdir /bin/mkdir -p /log/ipc /log/run /log/cluster_logs || exit 1
if [ ! -d /log/ipc ]; then echo 'QIP FAIL log-ipc-directory'; exit 1; fi
qm_bounded logprobe /bin/touch /log/ipc/owned-write-test || exit 1
if [ ! -f /log/ipc/owned-write-test ]; then echo 'QIP FAIL log-ipc-write'; exit 1; fi
echo 'QIP LOG writable=1'
qm_bounded userdirs /bin/mkdir -p /log/data /log/tmp || exit 1
qm_bounded dataprefix /bin/ln -sfP /log/data /data || exit 1
qm_bounded tmpprefix /bin/ln -sfP /log/tmp /tmp || exit 1
qm_bounded datatest /bin/touch /data/owned-write-test || exit 1
qm_bounded tmptest /bin/touch /tmp/owned-write-test || exit 1
if [ ! -f /data/owned-write-test ] || [ ! -f /tmp/owned-write-test ]; then echo 'QIP FAIL writable-user-paths'; exit 1; fi
echo 'QIP USER writable-data=1 writable-tmp=1'
qm_bounded vardirs /bin/mkdir -p /log/var/pps_persist /log/var/run || exit 1
qm_bounded varprefix /bin/ln -sfP /log/var /var || exit 1
qm_bounded vartest /bin/touch /var/run/owned-write-test || exit 1
if [ ! -f /var/run/owned-write-test ]; then echo 'QIP FAIL writable-var'; exit 1; fi
/bin/pps -m /var/pps -p /var/pps_persist -t 100 &
qm_pps=$!
qm_bounded pps-ready qm_wait_pps || exit 1
qm_bounded ppsroot /bin/ls -l /var/pps

{ /bin/cat /etc/dlt.conf; echo 'PrintASCII = 1'; echo 'LoggingMode = 3'; echo 'ContextLogLevel = 4'; } > /log/dlt-owned.conf
/usr/bin/dlt-daemon -c /log/dlt-owned.conf > /log/dlt-ascii.log 2>&1 &
qm_dlt=$!
qm_bounded dlt-ready qm_wait_dlt || exit 1

/usr/bin/sysmgr &
qm_sysmgr=$!
# Keep the original monitor, with every automatic unit launch disabled.
if [ ! -r /usr/bin/pmonitor ]; then echo 'QIP FAIL pmonitor-missing'; exit 1; fi
/bin/mkdir -p /log/clsboot
for qm_unit in 0_hmimgr 1_powermgr 2_setting 3_themmgr 4_updatemgr 5_rpcd 6_ivcd 7_mcd 8_logmgr 9_sysmgr 10_pmonitor 11_audiomgr 12_light 13_point 14_gauge 15_themebg 16_animation 17_alarmaudioapp 18_dlt-daemon 19_dlt-qnx-system 20_apapdcbeep 21_drawnv 22_pps 23_io-pkt-v6-hc 24_avmdatatrans 25_reversed; do echo "#$qm_unit"; done > /log/clsboot/unboot.list
qm_bounded monitor-config /bin/ln -sfP /log/clsboot/unboot.list /etc/clsboot/unboot.list || exit 1
qm_monitor_entries=$(/bin/grep -c '^#' /etc/clsboot/unboot.list)
if [ "$qm_monitor_entries" != 26 ]; then echo 'QIP FAIL monitor-config-not-shadowed'; exit 1; fi
if /bin/grep -q '^[^#]' /etc/clsboot/unboot.list; then echo 'QIP FAIL monitor-config-active-unit'; exit 1; fi
qm_bounded monitor-config-read /bin/cat /etc/clsboot/unboot.list
/usr/bin/pmonitor > /log/pmonitor-console.log 2>&1 &
qm_monitor=$!
/usr/bin/mcd &
qm_mcd=$!
qm_bounded mcd-ready qm_wait_core_ipc "$qm_mcd" /log/ipc/mcdserver/server.ipc /log/run/mcd.pid mcd || exit 1
QGFX_NATIVE_MCU=1 LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /usr/bin/rpcd dummy &
qm_rpcd=$!
qm_bounded rpcd-ready qm_wait_core_ipc "$qm_rpcd" /log/ipc/rpcsever/server.ipc /log/run/rpcd.pid rpcd || exit 1
/usr/bin/setting &
qm_setting=$!
/usr/bin/powermgr &
qm_power=$!
QGFX_MANAGER_OBSERVE=1 QGFX_MCD_TRACE=1 LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /usr/bin/themmgr &
qm_theme=$!
/usr/bin/ivcd 2>/log/ivcd-stderr.log &
qm_ivcd=$!
qm_observer_hash=$(/bin/openssl dgst -sha256 /proc/boot/bin/qditest)
case "$qm_observer_hash" in
    *2e13e9b8299f882414400969309c501c639314a92f5d0e603806e0f13d37426c) echo 'QIP OBSERVER IFS-padded-hash-verified' ;;
    *) echo 'QIP FAIL observer-IFS-hash'; exit 1 ;;
esac
qm_bounded observer-dependencies /usr/bin/ldd /proc/boot/bin/qditest
QGFX_MCD_TRACE=0 QGFX_MCD_OBSERVE=0 LD_PRELOAD=/proc/boot/bin/qditest /bin/ls -ld /log > /log/observer-loader-probe.log 2>&1
qm_probe_rc=$?
/bin/cat /log/observer-loader-probe.log
if [ "$qm_probe_rc" != 0 ] || /bin/grep -q 'FATAL:' /log/observer-loader-probe.log; then echo "QIP FAIL observer-IFS-loader-probe code=$qm_probe_rc"; exit 1; fi
echo 'QIP OBSERVER IFS-loader-probe-passed'
QGFX_MCD_TRACE=1 QGFX_MCD_OBSERVE=1 LD_PRELOAD=/proc/boot/bin/qditest /usr/bin/hmimgr > /log/hmi-console.log 2>&1 &
qm_hmi=$!
/bin/sleep 3
/bin/cat /log/hmi-console.log
if ! kill -0 "$qm_hmi" 2>/dev/null || /bin/grep -q 'FATAL:' /log/hmi-console.log; then echo 'QIP FAIL hmi-startup'; exit 1; fi
echo "QIP HMI startup-alive=$qm_hmi"
for qm_service in "$qm_mcd" "$qm_rpcd" "$qm_theme" "$qm_hmi" "$qm_ivcd"; do
    if kill -0 "$qm_service" 2>/dev/null; then echo "QIP SERVICE alive=$qm_service"; else wait "$qm_service"; echo "QIP SERVICE exit=$? pid=$qm_service"; fi
done
QGFX_MCD_TRACE=1 QGFX_MCD_OBSERVE=1 LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /usr/bin/themebg -config=/usr/share/theme/application_themebg.cfg &
qm_background=$!
QGFX_MCD_TRACE=1 QGFX_GL_BATCH=1 QGFX_UNIFORM_PREFETCH=1 QGFX_LANGUAGE_RELAY=1 QGFX_THEME_RELAY=1 LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /usr/bin/point -config=/usr/share/theme/application_point.cfg &
qm_point=$!
QGFX_MCD_TRACE=1 QGFX_MCD_OBSERVE=1 LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /usr/bin/light -config=/usr/share/theme/application_light.cfg &
qm_light=$!
QGFX_WHEEL_RELAY=0 QGFX_MCU_RELAY=0 QGFX_DLT_TAIL=1 QGFX_THEME_REQUEST_ENTRY=1 QGFX_MCD_TRACE=1 QGFX_GL_BATCH=1 QGFX_UNIFORM_PREFETCH=1 QGFX_LANGUAGE_RELAY=1 QGFX_THEME_RELAY=1 LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /usr/bin/gauge -config=/usr/share/theme/application_gauge.cfg &
qm_gauge=$!
# Stream the real Screen composition while OEM applications are initializing.
qm_export_screen &
qm_export=$!
if ! qm_wait_window "$qm_gauge" GAUGE 120; then echo 'QIP FAIL gauge-window'; kill "$qm_export" 2>/dev/null; exit 1; fi
qm_observer_ram_check || exit 1
/bin/grep -E 'Gauge:|ign=|IGN|set status|state=|status=|SCREEN|screen|theme.*ready' /log/dlt-ascii.log > /log/dlt-state.log
qm_bounded dlt-state /usr/bin/tail -40 /log/dlt-state.log
qm_screen_windows() {
    /bin/ls -l /dev/screen
    for qm_window_pid in "$qm_background" "$qm_point" "$qm_light" "$qm_gauge"; do
        echo "QIP WINDOW pid=$qm_window_pid"
        /bin/ls -l /dev/screen/"$qm_window_pid"
        for qm_window_file in /dev/screen/"$qm_window_pid"/win-* /dev/screen/"$qm_window_pid"/win-*/*; do
            case "$qm_window_file" in *.bmp) continue ;; esac
            if [ -r "$qm_window_file" ] && [ ! -d "$qm_window_file" ]; then
                echo "QIP WINDOW file=$qm_window_file"
                /bin/cat "$qm_window_file" | /bin/grep -E 'id string|status =|order =|buffer size|format =|usage =|source viewport|destination rectangle|transparency =|global alpha|class =|parent =|root =|window above|window below'
            fi
        done
    done
}
qm_bounded screen-windows qm_screen_windows
qm_bounded ivcd-paths /bin/ls -l /log/ipc/ivcdserver/server.ipc
wait "$qm_export"
echo "QC1 SCREENSHOT exit=$?"
kill "$qm_gauge" 2>/dev/null
wait "$qm_gauge"
echo "QMC GAUGE exit=$?"
for qm_service in "$qm_background" "$qm_point" "$qm_light" "$qm_ivcd" "$qm_hmi" "$qm_pps" "$qm_dlt" "$qm_monitor" "$qm_sysmgr" "$qm_setting" "$qm_power" "$qm_mcd" "$qm_rpcd" "$qm_theme" "$qm_hmi" "$qm_ivcd"; do kill "$qm_service" 2>/dev/null; wait "$qm_service"; done
echo 'QIP DONE'
echo 'QMC DONE'
exit 0
