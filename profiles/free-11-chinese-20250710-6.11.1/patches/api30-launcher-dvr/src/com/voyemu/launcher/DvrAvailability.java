package com.voyemu.launcher;

import android.content.Context;
import android.graphics.ImageFormat;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.params.StreamConfigurationMap;

public final class DvrAvailability {
    public static boolean useService(Object intent) {
        return intent == null && select(0) == 1;
    }

    public static int select(int original) {
        if (original == 1) {
            return original;
        }

        try {
            Context context =
                    (Context)
                            Class.forName("com.qinggan.launcher.base.BaseApplication")
                                    .getMethod("getContext")
                                    .invoke(null);

            if (context == null) {
                return original;
            }

            context.getPackageManager().getPackageInfo("com.qinggan.camera", 0);
            CameraManager manager =
                    (CameraManager) context.getSystemService(Context.CAMERA_SERVICE);

            if (manager == null) {
                return original;
            }

            for (String id : manager.getCameraIdList()) {
                CameraCharacteristics c = manager.getCameraCharacteristics(id);
                Integer facing = c.get(CameraCharacteristics.LENS_FACING);
                StreamConfigurationMap map =
                        c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);

                if (facing != null
                        && facing == CameraCharacteristics.LENS_FACING_BACK
                        && map != null
                        && map.getOutputSizes(ImageFormat.JPEG) != null
                        && map.getOutputSizes(ImageFormat.JPEG).length > 0) {
                    return 1;
                }
            }
        } catch (Exception ignored) {
            // checkstyle-allow IllegalCatch: reflection or camera service failure retains OEM DVR
            // availability
            // An unavailable camera service keeps the original DVR availability.
        }

        return original;
    }
}
