package android.app;

public final class ActivityThread {
    public static Application application;

    public static Application currentApplication() {
        return application;
    }
}
