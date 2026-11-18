package android.app;

public final class ActivityManager {
    public static IActivityManager service = new IActivityManager();
    public static int calls;

    private ActivityManager() {}

    public static IActivityManager getService() {
        calls++;

        return service;
    }
}
