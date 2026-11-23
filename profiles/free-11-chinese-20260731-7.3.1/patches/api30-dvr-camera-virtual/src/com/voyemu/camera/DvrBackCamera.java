package com.voyemu.camera;

import android.content.Context;
import android.graphics.ImageFormat;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.util.Size;

import java.lang.reflect.Method;
import java.util.Collections;
import java.util.List;

/** Exposes the actual emulator BACK camera to the OEM DVR CameraX preview. */
public final class DvrBackCamera {
    private DvrBackCamera() {}

    public static int capability(int original) {
        return original == -1 && backCameraId() != null ? 1 : original;
    }

    public static List<?> select(List<?> cameras, List<?> original) {
        if (!original.isEmpty()) {
            return original;
        }

        String id = backCameraId();

        if (id == null) {
            return original;
        }

        try {
            Class<?> infoType = Class.forName("androidx.camera.core.impl.CameraInfoInternal");
            Method idGetter = infoType.getMethod("getCameraId");
            Object selected = null;

            for (Object camera : cameras) {
                if (id.equals(idGetter.invoke(camera))) {
                    if (selected != null) {
                        return original;
                    }

                    selected = camera;
                }
            }

            return selected == null ? original : Collections.singletonList(selected);
        } catch (ReflectiveOperationException | RuntimeException unavailable) {
            // checkstyle-allow IllegalCatch: CameraX ABI failure retains OEM selection.
            return original;
        }
    }

    private static String backCameraId() {
        try {
            Object application =
                    Class.forName("com.qinggan.dvr.AppMain").getMethod("getContext").invoke(null);

            if (!(application instanceof Context)) {
                return null;
            }

            CameraManager manager = ((Context) application).getSystemService(CameraManager.class);

            if (manager == null) {
                return null;
            }

            String selected = null;

            for (String id : manager.getCameraIdList()) {
                CameraCharacteristics camera = manager.getCameraCharacteristics(id);
                Integer facing = camera.get(CameraCharacteristics.LENS_FACING);
                StreamConfigurationMap streams =
                        camera.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);

                if (facing == null
                        || facing != CameraCharacteristics.LENS_FACING_BACK
                        || streams == null) {
                    continue;
                }

                Size[] sizes = streams.getOutputSizes(ImageFormat.JPEG);

                if (sizes == null || sizes.length == 0) {
                    continue;
                }

                if (selected != null) {
                    return null;
                }

                selected = id;
            }

            return selected;
        } catch (ReflectiveOperationException
                | CameraAccessException
                | RuntimeException unavailable) {
            // checkstyle-allow IllegalCatch: camera access failure is not a capability.
            return null;
        }
    }
}
