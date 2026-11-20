package com.voyemu.navi;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.os.Process;

import java.lang.reflect.Field;
import java.lang.reflect.Method;

public final class OwnContext {
    private static final String PACKAGE = "com.baidu.naviauto";

    private static boolean valid(Context c, int uid) {
        if (c == null
                || !PACKAGE.equals(c.getPackageName())
                || !PACKAGE.equals(c.getOpPackageName())) {
            return false;
        }

        ApplicationInfo info = c.getApplicationInfo();

        if (info == null || info.uid != uid || !PACKAGE.equals(info.packageName)) {
            return false;
        }

        String[] packages = c.getPackageManager().getPackagesForUid(uid);

        return packages != null && packages.length == 1 && PACKAGE.equals(packages[0]);
    }

    private static Context boundContext(Class<?> thread, int uid)
            throws ReflectiveOperationException {
        Object instance = thread.getMethod("currentActivityThread").invoke(null);

        if (instance == null) {
            return null;
        }

        Field bound = thread.getDeclaredField("mBoundApplication");
        bound.setAccessible(true);
        Object binding = bound.get(instance);

        if (binding == null) {
            return null;
        }

        Field info = binding.getClass().getDeclaredField("info");
        info.setAccessible(true);
        Object loaded = info.get(binding);
        Class<?> loadedClass = Class.forName("android.app.LoadedApk");

        if (!loadedClass.isInstance(loaded)) {
            return null;
        }

        Object value = loadedClass.getMethod("getApplicationInfo").invoke(loaded);

        if (!(value instanceof ApplicationInfo)) {
            return null;
        }

        ApplicationInfo identity = (ApplicationInfo) value;

        if (identity.uid != uid || !PACKAGE.equals(identity.packageName)) {
            return null;
        }

        Method create =
                Class.forName("android.app.ContextImpl")
                        .getDeclaredMethod("createAppContext", thread, loadedClass);
        create.setAccessible(true);
        Object own = create.invoke(null, instance, loaded);

        return own instanceof Context && valid((Context) own, uid) ? (Context) own : null;
    }

    public static Context get() {
        try {
            int uid = Process.myUid();

            if (uid < 10000) {
                return null;
            }

            Class<?> thread = Class.forName("android.app.ActivityThread");

            if (!PACKAGE.equals(thread.getMethod("currentPackageName").invoke(null))) {
                return null;
            }

            Object app = thread.getMethod("currentApplication").invoke(null);

            if (app != null) {
                return app instanceof Context && valid((Context) app, uid) ? (Context) app : null;
            }

            return boundContext(thread, uid);
        } catch (Exception | LinkageError e) {
            // checkstyle-allow IllegalCatch: any unavailable Android reflection or identity check
            // fails closed
            return null;
        }
    }
}
