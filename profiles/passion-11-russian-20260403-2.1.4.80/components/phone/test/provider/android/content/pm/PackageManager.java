package android.content.pm;

public class PackageManager {
    public static final int PERMISSION_GRANTED = 0;

    public static final class ApplicationInfo {
        public int uid = 1000;
    }

    public static class NameNotFoundException extends Exception {}

    public ApplicationInfo getApplicationInfo(String name, int flags) throws NameNotFoundException {
        return new ApplicationInfo();
    }
}
