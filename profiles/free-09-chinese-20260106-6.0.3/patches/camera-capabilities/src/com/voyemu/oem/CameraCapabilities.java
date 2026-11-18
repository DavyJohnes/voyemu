package com.voyemu.oem;

import android.content.Context;
import android.graphics.ImageFormat;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.util.Log;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;

/** APK-local boundary: explicit virtual BACK camera only; NVRAM and DMS hardware stay untouched. */
public final class CameraCapabilities {
    private static final String TAG = "VoyemuCamera";
    private static final String PROFILE = "free-09-chinese-20260106-6.0.3";

    private CameraCapabilities() {}

    private static Context context() throws Exception {
        Object value = Class.forName("com.qinggan.camera.AppMain").getField("context").get(null);

        if (!(value instanceof Context)) {
            throw new IllegalStateException("camera-context-unavailable");
        }

        return ((Context) value).getApplicationContext();
    }

    // ByteArrayOutputStream.toString(Charset) needs API 33; the guest is API 28.
    @SuppressWarnings("JdkObsolete")
    private static String backend(Context context) throws Exception {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();

        try (InputStream input = context.getAssets().open("voyemu-camera-capabilities.json")) {
            byte[] buffer = new byte[512];
            int count;

            while ((count = input.read(buffer)) != -1) {
                if (bytes.size() + count > 4096) {
                    throw new IllegalArgumentException("camera-contract-size");
                }

                bytes.write(buffer, 0, count);
            }
        }

        JSONObject object = new JSONObject(bytes.toString("UTF-8"));

        if (object.getInt("schema") != 1
                || !PROFILE.equals(object.getString("profile"))
                || object.getBoolean("physicalDms")
                || object.getBoolean("physicalRpm")) {
            throw new IllegalArgumentException("camera-contract");
        }

        String backend = object.getString("backend");
        CameraCapabilityPolicy.supportsDvr(backend, false);

        return backend;
    }

    /** Replacement only for this APK's SupportUtils.isSupportDvr; no global equipment mutation. */
    public static boolean isSupportDvr() {
        try {
            Context context = context();
            String backend = backend(context);

            if (CameraCapabilityPolicy.NONE.equals(backend)) {
                return false;
            }

            CameraManager manager =
                    (CameraManager) context.getSystemService(Context.CAMERA_SERVICE);

            if (manager == null) {
                return false;
            }

            for (String id : manager.getCameraIdList()) {
                CameraCharacteristics camera = manager.getCameraCharacteristics(id);
                Integer facing = camera.get(CameraCharacteristics.LENS_FACING);
                StreamConfigurationMap streams =
                        camera.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);

                if (facing != null
                        && facing == CameraCharacteristics.LENS_FACING_BACK
                        && streams != null
                        && streams.getOutputSizes(ImageFormat.JPEG) != null
                        && streams.getOutputSizes(ImageFormat.JPEG).length > 0) {
                    Log.i(TAG, "INFO virtual-back-camera-advertised; capture-not-yet-verified");

                    return true;
                }
            }
        } catch (Exception error) {
            // checkstyle-allow IllegalCatch: The OEM capability boundary fails closed on camera,
            // contract or reflection failure.
            Log.e(TAG, "ERROR camera-capability-unavailable " + error.getClass().getSimpleName());
        }

        return false;
    }

    /** Replaces the OEM root-gallery quota gate only in the explicit virtual-backend recipe. */
    public static float getStatFsRatio(String originalPath) {
        try {
            CameraStoragePolicy.requireGalleryRoot(originalPath);
            Context context = context();

            if (!CameraCapabilityPolicy.ANDROID_BACK.equals(backend(context))) {
                return 0.0f;
            }

            java.io.File directory = CameraStoragePolicy.directory(context.getFilesDir());

            return CameraStoragePolicy.freePercent(
                    directory.getUsableSpace(), directory.getTotalSpace());
        } catch (Exception error) {
            // checkstyle-allow IllegalCatch: The OEM quota boundary fails closed on private-storage
            // or reflection failure.
            Log.e(
                    TAG,
                    "ERROR camera-private-storage-unavailable " + error.getClass().getSimpleName());

            return 0.0f;
        }
    }

    /** App-private output, no root /gallery mount or broad storage permission required. */
    // SimpleDateFormat with Date keeps the original timestamp format unchanged.
    @SuppressWarnings("JavaUtilDate")
    public static java.io.File getOutputMediaFileNew(int type) {
        try {
            Context context = context();

            if (!CameraCapabilityPolicy.ANDROID_BACK.equals(backend(context))) {
                return null;
            }

            String timestamp =
                    new java.text.SimpleDateFormat("yyyyMMdd_HHmmss", java.util.Locale.US)
                            .format(new java.util.Date());

            return CameraStoragePolicy.reserve(context.getFilesDir(), type, timestamp);
        } catch (Exception error) {
            // checkstyle-allow IllegalCatch: The OEM output boundary returns no file on storage,
            // contract or reflection failure.
            Log.e(TAG, "ERROR camera-output-unavailable " + error.getClass().getSimpleName());

            return null;
        }
    }

    /** Private completed capture thumbnail; no QGMediaStore query or external viewer. */
    public static java.io.File getLatestSource() {
        try {
            Context context = context();

            if (!CameraCapabilityPolicy.ANDROID_BACK.equals(backend(context))) {
                return null;
            }

            return CameraStoragePolicy.latestCompletedJpeg(context.getFilesDir());
        } catch (Exception error) {
            // checkstyle-allow IllegalCatch: The OEM gallery boundary returns no source on storage,
            // contract or reflection failure.
            Log.e(TAG, "ERROR camera-gallery-unavailable " + error.getClass().getSimpleName());

            return null;
        }
    }

    /** OEM DMS coordination marker is local here because this profile declares no DMS ECU. */
    public static void setDmsInCameraAppUsing(int value) {
        int marker = CameraCapabilityPolicy.dmsCoordinationMarker(value);

        try {
            if (!context()
                    .getSharedPreferences("voyemu-camera-coordination", Context.MODE_PRIVATE)
                    .edit()
                    .putInt("global_camera_dms_using", marker)
                    .commit()) {
                throw new IllegalStateException("camera-marker-storage");
            }

            Log.d(TAG, "DEBUG local-camera-coordination-updated");
        } catch (Exception error) {
            // checkstyle-allow IllegalCatch: The OEM marker boundary records Android persistence or
            // reflection failure.
            Log.e(TAG, "ERROR camera-marker-unavailable " + error.getClass().getSimpleName());
        }
    }
}
