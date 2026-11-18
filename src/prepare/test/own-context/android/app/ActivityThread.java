package android.app;

import android.content.Context;

public final class ActivityThread {
    public static String name = "com.baidu.naviauto";
    public static Context app;
    public static ActivityThread instance = new ActivityThread();
    private AppBindData mBoundApplication = new AppBindData();

    private static final class AppBindData {
        private LoadedApk info = new LoadedApk();
    }

    public static String currentPackageName() {
        return name;
    }

    public static Context currentApplication() {
        return app;
    }

    public static ActivityThread currentActivityThread() {
        return instance;
    }

    public Context getSystemContext() {
        Context c = new Context();

        c.packageName = "android";
        c.opPackageName = "android";

        return c;
    }

    public static LoadedApk bound() {
        return instance.mBoundApplication.info;
    }

    public static void missingLoaded() {
        instance.mBoundApplication.info = null;
    }

    public static void reset() {
        instance = new ActivityThread();
        app = null;
        name = "com.baidu.naviauto";
        android.os.Process.uid = 10130;
    }
}
