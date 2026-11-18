package android.app;

import android.content.Context;

public final class ContextImpl {
    public static int creations;

    // Android11 createAppContext uses a null container: identity comes from the bound LoadedApk.
    private static Context createAppContext(ActivityThread thread, LoadedApk info) {
        creations++;

        return info.context;
    }
}
