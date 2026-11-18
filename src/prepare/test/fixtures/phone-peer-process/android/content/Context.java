package android.content;

public class Context {
    public static final String ACTIVITY_SERVICE = "activity";

    public android.os.Looper getMainLooper() {
        return android.os.Looper.MAIN;
    }

    public Context getApplicationContext() {
        return this;
    }

    public Object getSystemService(String name) {
        return new android.app.ActivityManager();
    }

    public android.content.pm.PackageManager getPackageManager() {
        return new android.content.pm.PackageManager();
    }

    public java.io.File getFilesDir() {
        java.io.File path =
                new java.io.File(
                        System.getProperty("phone.peer.test.files"),
                        android.os.PeerTestRuntime.CURRENT.get().name);

        path.mkdirs();

        return path;
    }

    public ClassLoader getClassLoader() {
        return getClass().getClassLoader();
    }

    public ContentResolver getContentResolver() {
        return new ContentResolver();
    }
}
