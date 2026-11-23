package android.os.storage;

public abstract class VolumeInfo {
    public abstract String getId();
    public abstract int getType();
    public abstract DiskInfo getDisk();
}
