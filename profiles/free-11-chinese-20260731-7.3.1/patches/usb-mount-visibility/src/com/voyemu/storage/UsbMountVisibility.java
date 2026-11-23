package com.voyemu.storage;

import android.os.storage.DiskInfo;
import android.os.storage.VolumeInfo;

import java.util.regex.Pattern;

/** Keeps the SDK's existing visible mounts and exposes the owned test USB transport. */
public final class UsbMountVisibility {
    private static final int TYPE_PUBLIC = 0;
    private static final long VOYEMU_DISK_BYTES = 64L * 1024 * 1024;
    private static final Pattern EHCI_TEST_DISK = Pattern.compile(
            "/sys//devices/platform/3f000000\\.pcie/pci0000:00/0000:00:0e\\.0"
                    + "/usb1/1-1/1-1:1\\.0/host[0-9]+/target[0-9]+:0:0/[0-9]+:0:0:0/block/sda");

    private UsbMountVisibility() {}

    public static boolean allow(boolean isAdoptable, VolumeInfo volume) {
        if (isAdoptable) {
            return true;
        }
        if (volume == null || volume.getType() != TYPE_PUBLIC
                || !"public:8,1".equals(volume.getId())) {
            return false;
        }

        DiskInfo disk = volume.getDisk();
        return disk != null && disk.isUsb() && !disk.isSd()
                && "disk:8,0".equals(disk.getId())
                && disk.size == VOYEMU_DISK_BYTES
                && "QEMU".equals(disk.label)
                && disk.sysPath != null
                && EHCI_TEST_DISK.matcher(disk.sysPath).matches();
    }
}
