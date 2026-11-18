package android.hardware.camera2.params;

public class StreamConfigurationMap {
    public boolean jpeg;

    public Object[] getOutputSizes(int i) {
        return jpeg ? new Object[] {new Object()} : null;
    }
}
