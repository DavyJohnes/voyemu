package android.content;

public class Context {
    public static final String ACTIVITY_SERVICE = "activity";
    public static android.app.ActivityManager manager = new android.app.ActivityManager();

    public android.os.Looper getMainLooper() {
        return android.os.Looper.MAIN;
    }

    public Object getSystemService(String key) {
        return manager;
    }

    public android.content.pm.PackageManager getPackageManager() {
        return new android.content.pm.PackageManager();
    }

    public int checkCallingPermission(String permission) {
        return android.content.pm.PackageManager.PERMISSION_GRANTED;
    }

    public ClassLoader getClassLoader() {
        return getClass().getClassLoader();
    }
}
