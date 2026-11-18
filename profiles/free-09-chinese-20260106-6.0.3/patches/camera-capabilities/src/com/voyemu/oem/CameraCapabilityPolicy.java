package com.voyemu.oem;

/** Profile-owned virtual capability, never a declaration of physical vehicle equipment. */
public final class CameraCapabilityPolicy {
    public static final String NONE = "none";
    public static final String ANDROID_BACK = "android-back-camera";

    private CameraCapabilityPolicy() {}

    public static boolean supportsDvr(String backend, boolean backCameraWithOutputs) {
        if (!NONE.equals(backend) && !ANDROID_BACK.equals(backend)) {
            throw new IllegalArgumentException("unknown-camera-backend");
        }

        return ANDROID_BACK.equals(backend) && backCameraWithOutputs;
    }

    public static int dmsCoordinationMarker(int value) {
        if (value != 0 && value != 1) {
            throw new IllegalArgumentException("invalid-dms-marker");
        }

        return value;
    }
}
