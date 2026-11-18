package com.voyemu.camera;

import android.content.Context;
import android.graphics.ImageFormat;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.util.Log;

public final class BackCameraProbe {
    public static boolean isSupported() {
        try {
            Context context =
                    (Context)
                            Class.forName("com.qinggan.camera.AppMain")
                                    .getField("context")
                                    .get(null);
            CameraManager manager =
                    (CameraManager) context.getSystemService(Context.CAMERA_SERVICE);

            for (String id : manager.getCameraIdList()) {
                CameraCharacteristics c = manager.getCameraCharacteristics(id);
                Integer f = c.get(CameraCharacteristics.LENS_FACING);
                StreamConfigurationMap m =
                        c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);

                if (f != null
                        && f == CameraCharacteristics.LENS_FACING_BACK
                        && m != null
                        && m.getOutputSizes(ImageFormat.JPEG) != null
                        && m.getOutputSizes(ImageFormat.JPEG).length > 0) {
                    Log.i("BackCameraProbe", "BACK JPEG available");

                    return true;
                }
            }
        } catch (Exception e) {
            // checkstyle-allow IllegalCatch: reflection or camera service failure keeps BACK
            // support unavailable
            Log.e("BackCameraProbe", "backend unavailable", e);
        }

        return false;
    }

    public static String selectId(String original) {
        try {
            Class<?> setting = Class.forName("com.qinggan.camera.common.CameraSetting");
            Object instance = setting.getMethod("getInstance").invoke(null);
            int type = (Integer) setting.getMethod("getCameraType").invoke(instance);

            if (type != 2) {
                return original;
            }

            Context context =
                    (Context)
                            Class.forName("com.qinggan.camera.AppMain")
                                    .getField("context")
                                    .get(null);
            CameraManager manager =
                    (CameraManager) context.getSystemService(Context.CAMERA_SERVICE);

            for (String id : manager.getCameraIdList()) {
                CameraCharacteristics c = manager.getCameraCharacteristics(id);
                Integer f = c.get(CameraCharacteristics.LENS_FACING);
                StreamConfigurationMap m =
                        c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);

                if (f != null
                        && f == CameraCharacteristics.LENS_FACING_BACK
                        && m != null
                        && m.getOutputSizes(ImageFormat.JPEG) != null
                        && m.getOutputSizes(ImageFormat.JPEG).length > 0) {
                    return id;
                }
            }
        } catch (Exception e) {
            // checkstyle-allow IllegalCatch: reflection or camera service failure retains the
            // original camera selection
            Log.e("BackCameraProbe", "selection unavailable", e);
        }

        return original;
    }

    public static java.util.List augment(java.util.List input, java.util.List filtered) {
        try {
            Context c =
                    (Context)
                            Class.forName("com.qinggan.camera.AppMain")
                                    .getField("context")
                                    .get(null);
            CameraManager mgr = (CameraManager) c.getSystemService(Context.CAMERA_SERVICE);
            Class<?> type = Class.forName("androidx.camera.core.impl.CameraInfoInternal");

            for (Object info : input) {
                String id = (String) type.getMethod("getCameraId").invoke(info);
                CameraCharacteristics chars = mgr.getCameraCharacteristics(id);
                Integer facing = chars.get(CameraCharacteristics.LENS_FACING);
                StreamConfigurationMap map =
                        chars.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);

                if (facing != null
                        && facing == CameraCharacteristics.LENS_FACING_BACK
                        && map != null
                        && map.getOutputSizes(ImageFormat.JPEG) != null
                        && map.getOutputSizes(ImageFormat.JPEG).length > 0
                        && !filtered.contains(info)) {
                    filtered.add(info);
                }
            }
        } catch (Exception e) {
            // checkstyle-allow IllegalCatch: reflection or camera service failure preserves the
            // current partial limiter result
            Log.e("BackCameraProbe", "limiter augmentation interrupted", e);
        }

        return filtered;
    }

    public static int localEntry(int entry) {
        return entry == 1 && isSupported() ? 0 : entry;
    }

    public static int defaultType(int original) {
        return isSupported() ? 2 : original;
    }

    public static Object preferredQuality(Object original) {
        try {
            String id = selectId("");
            int camera = Integer.parseInt(id);

            if (android.media.CamcorderProfile.hasProfile(
                    camera, android.media.CamcorderProfile.QUALITY_1080P)) {
                return original;
            }

            if (android.media.CamcorderProfile.hasProfile(
                    camera, android.media.CamcorderProfile.QUALITY_720P)) {
                return Class.forName("androidx.camera.video.Quality").getField("HD").get(null);
            }
        } catch (Exception e) {
            // checkstyle-allow IllegalCatch: reflection or camera profile failure retains the
            // original quality
            Log.e("BackCameraProbe", "quality unavailable", e);
        }

        return original;
    }
}
