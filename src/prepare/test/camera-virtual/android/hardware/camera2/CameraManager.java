package android.hardware.camera2;

import java.util.LinkedHashMap;
import java.util.Map;

public class CameraManager {
    public final Map<String, CameraCharacteristics> cameras = new LinkedHashMap<>();

    public String[] getCameraIdList() {
        return cameras.keySet().toArray(new String[0]);
    }

    public CameraCharacteristics getCameraCharacteristics(String id) {
        return cameras.get(id);
    }
}
