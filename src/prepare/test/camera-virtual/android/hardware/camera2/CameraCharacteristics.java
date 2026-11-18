package android.hardware.camera2;

import android.hardware.camera2.params.StreamConfigurationMap;

public class CameraCharacteristics {
    public static class Key<T> {}

    public static final Key<Integer> LENS_FACING = new Key<>();
    public static final Key<StreamConfigurationMap> SCALER_STREAM_CONFIGURATION_MAP = new Key<>();
    public static final int LENS_FACING_BACK = 1;
    public Integer facing;
    public StreamConfigurationMap map;

    @SuppressWarnings("unchecked")
    public <T> T get(Key<T> key) {
        return (T) (key == LENS_FACING ? facing : map);
    }
}
