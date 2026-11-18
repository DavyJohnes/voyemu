package android.content;

public class Context {
    public static int permission = 0;

    public int checkCallingPermission(String p) {
        if (!p.equals("android.permission.DUMP")) {
            throw new AssertionError();
        }

        return permission;
    }

    public android.os.Looper getMainLooper() {
        return android.os.Looper.MAIN;
    }

    public ClassLoader getClassLoader() {
        return getClass().getClassLoader();
    }

    public android.content.pm.PackageManager getPackageManager() {
        return new android.content.pm.PackageManager();
    }
}
