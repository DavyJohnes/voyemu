package android.os.storage;

public abstract class DiskInfo {
    public long size;
    public String label;
    public String sysPath;
    public abstract String getId();
    public abstract boolean isUsb();
    public abstract boolean isSd();
}
