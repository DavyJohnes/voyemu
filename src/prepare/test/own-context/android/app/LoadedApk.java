package android.app;

public final class LoadedApk {
    public android.content.Context context = new android.content.Context();

    public android.content.pm.ApplicationInfo getApplicationInfo() {
        return context.info;
    }
}
