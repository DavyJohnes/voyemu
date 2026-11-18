package android.app;

import android.content.Context;

public class ActivityThread {
    public static String p;
    public static Application a;
    public static boolean fail;
    public static ActivityThread current;
    public Context system;
    public Object mBoundApplication;

    public static String currentPackageName() {
        if (fail) {
            throw new IllegalStateException();
        }

        return p;
    }

    public static Application currentApplication() {
        return a;
    }

    public static ActivityThread currentActivityThread() {
        return current;
    }

    public Context getSystemContext() {
        return system;
    }
}
