#!/vendor/bin/sh
# Gives the interguest NIC the car's multimedia Ethernet name eth0 after the emulator's own eth0 has
# moved into the router namespace; the OEM network services start from vendor.qemu.oem_eth0.
mac=52:54:00:76:6f:21
nic=$(/vendor/bin/ip -o link show | while read -r index name rest; do
    case "$rest" in
    *"link/ether $mac "*) echo "${name%:}" ;;
    esac
done)

if [ -z "$nic" ]; then
    log -p e -t voyemu "oem eth0: interguest NIC $mac missing"
    exit 1
fi

if [ "$nic" != eth0 ]; then
    if /vendor/bin/ip link show dev eth0 >/dev/null 2>&1; then
        log -p e -t voyemu "oem eth0: eth0 is still the emulator NIC; $nic keeps its name"
        exit 1
    fi

    /vendor/bin/ip link set dev "$nic" down || exit 1
    /vendor/bin/ip link set dev "$nic" name eth0 || exit 1
fi

/vendor/bin/ip link set dev eth0 up || exit 1
log -t voyemu "oem eth0: $nic is eth0"
setprop vendor.qemu.oem_eth0 ready
