package com.voyemu.qgapi;

import android.content.Context;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;

import java.io.File;
import java.lang.reflect.Method;

/** Resolves the one synthetic USB stick to the Free 7.3.1 physical port ID. */
public final class VoyemuUsbPort {
    private static final String UUID = "2026-1122";
    private static final File APP_DIRECTORY = new File("/storage/" + UUID);
    private static final int TYPE_PUBLIC = 0;

    private VoyemuUsbPort() {}

    public static String resolve(
            String oemHostId, Context context, Object entry, Object[] enumerated) {
        if ((oemHostId != null && !oemHostId.isEmpty())
                || context == null
                || !(entry instanceof StorageVolume)
                || enumerated == null) {
            return oemHostId;
        }

        try {
            // If OEM firmware knows physical port IDs, keep its authority even for empty IDs.
            for (Method method : StorageVolume.class.getDeclaredMethods()) {
                if (method.getName().equals("getHostId") && method.getParameterCount() == 0) {
                    return oemHostId;
                }
            }

            StorageVolume candidate = (StorageVolume) entry;
            if (!UUID.equals(candidate.getUuid()) || !APP_DIRECTORY.equals(candidate.getDirectory())
                    || !isReadableRemovable(candidate)) {
                return oemHostId;
            }

            StorageManager storage = context.getSystemService(StorageManager.class);
            if (storage == null) {
                return oemHostId;
            }

            Method getId = StorageVolume.class.getMethod("getId");
            Method lookup = StorageManager.class.getMethod("findVolumeById", String.class);
            String candidateId = (String) getId.invoke(candidate);
            if (candidateId == null || !candidateId.startsWith("public:")) {
                return oemHostId;
            }

            int matchingEntryCount = 0;
            int mountedUsbCount = 0;
            for (Object item : enumerated) {
                if (!(item instanceof StorageVolume)) {
                    return oemHostId;
                }

                StorageVolume volume = (StorageVolume) item;
                String id = (String) getId.invoke(volume);
                if (volume == candidate) {
                    matchingEntryCount++;
                } else if (UUID.equals(volume.getUuid())
                        || APP_DIRECTORY.equals(volume.getDirectory())
                        || candidateId.equals(id)) {
                    return oemHostId;
                }

                if (isReadableRemovable(volume)
                        && id != null
                        && id.startsWith("public:")
                        && isPublicUsb(lookup.invoke(storage, id))) {
                    mountedUsbCount++;
                    if (volume != candidate) {
                        return oemHostId;
                    }
                }
            }

            // OEM mapHostIdToPresent maps physical usb3 to the visible USB1 tab.
            if (matchingEntryCount == 1 && mountedUsbCount == 1) {
                return "usb3";
            }
        } catch (ReflectiveOperationException | RuntimeException | LinkageError unavailable) {
            // An unavailable platform ABI leaves the original no-port result intact.
        }
        return oemHostId;
    }

    private static boolean isReadableRemovable(StorageVolume volume) {
        return volume.isRemovable() && !volume.isEmulated()
                && ("mounted".equals(volume.getState())
                    || "mounted_ro".equals(volume.getState()));
    }

    private static boolean isPublicUsb(Object info) throws ReflectiveOperationException {
        if (info == null) {
            throw new IllegalStateException("Unknown public volume");
        }
        if (!Integer.valueOf(TYPE_PUBLIC).equals(call(info, "getType"))
                || !Boolean.TRUE.equals(call(info, "isMountedReadable"))) {
            return false;
        }

        Object disk = call(info, "getDisk");
        if (disk == null) {
            throw new IllegalStateException("Unknown public disk");
        }
        return Boolean.TRUE.equals(call(disk, "isUsb"))
                && Boolean.FALSE.equals(call(disk, "isSd"))
                && Boolean.FALSE.equals(call(disk, "isAdoptable"));
    }

    private static Object call(Object target, String name) throws ReflectiveOperationException {
        return target.getClass().getMethod(name).invoke(target);
    }
}
