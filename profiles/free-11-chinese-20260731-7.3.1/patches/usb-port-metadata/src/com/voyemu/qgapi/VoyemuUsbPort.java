package com.voyemu.qgapi;

import android.os.storage.StorageVolume;

import java.io.File;
import java.lang.reflect.Method;

/** Supplies an OEM port ID for the one synthetic volume exported by the emulator. */
public final class VoyemuUsbPort {
    private static final String UUID = "2026-1122";
    private static final File APP_DIRECTORY = new File("/storage/" + UUID);

    private VoyemuUsbPort() {}

    public static String resolve(String oemHostId, Object entry, Object[] enumerated) {
        if ((oemHostId != null && !oemHostId.isEmpty())
                || !(entry instanceof StorageVolume)
                || enumerated == null) {
            return oemHostId;
        }

        try {
            StorageVolume candidate = (StorageVolume) entry;

            // The original OEM map converts physical usb3 to the visible USB1 tab.
            if (!hasOemPortId() && matches(candidate) && isUnique(candidate, enumerated)) {
                return "usb3";
            }
        } catch (SecurityException | LinkageError unavailable) {
            // Unavailable platform metadata cannot identify an emulator drive.
        }

        return oemHostId;
    }

    private static boolean hasOemPortId() {
        // Keep the real OEM accessor authoritative, even when it returns an empty ID.
        for (Method method : StorageVolume.class.getDeclaredMethods()) {
            if (method.getName().equals("getHostId") && method.getParameterCount() == 0) {
                return true;
            }
        }

        return false;
    }

    private static boolean matches(StorageVolume volume) {
        return UUID.equals(volume.getUuid())
                && APP_DIRECTORY.equals(volume.getDirectory())
                && isReadableRemovable(volume);
    }

    private static boolean isUnique(StorageVolume candidate, Object[] enumerated) {
        int matchingEntries = 0;

        for (Object item : enumerated) {
            if (!(item instanceof StorageVolume)) {
                return false;
            }

            StorageVolume volume = (StorageVolume) item;

            if (volume == candidate) {
                matchingEntries++;
            } else if (UUID.equals(volume.getUuid())
                    || APP_DIRECTORY.equals(volume.getDirectory())
                    || isReadableRemovable(volume)) {
                return false;
            }
        }

        return matchingEntries == 1;
    }

    private static boolean isReadableRemovable(StorageVolume volume) {
        return volume.isRemovable()
                && !volume.isEmulated()
                && ("mounted".equals(volume.getState()) || "mounted_ro".equals(volume.getState()));
    }
}
