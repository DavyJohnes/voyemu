package android.app;

import android.content.Context;

public class ContextImpl {
    public static Context createAppContext(ActivityThread thread, android.app.LoadedApk loaded) {
        return thread.system.own;
    }
}
