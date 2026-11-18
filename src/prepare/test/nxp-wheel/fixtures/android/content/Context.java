package android.content;

public class Context {
    public static final int CONTEXT_INCLUDE_CODE = 1,
            CONTEXT_IGNORE_SECURITY = 2,
            BIND_AUTO_CREATE = 1;

    public Context getApplicationContext() {
        return this;
    }

    public Context createPackageContext(String name, int flags) {
        return this;
    }

    public ClassLoader getClassLoader() {
        return getClass().getClassLoader();
    }

    public boolean bindService(Intent intent, ServiceConnection connection, int flags) {
        return false;
    }
}
