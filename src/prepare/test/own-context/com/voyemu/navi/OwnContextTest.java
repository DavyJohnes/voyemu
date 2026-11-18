package com.voyemu.navi;

import android.app.ActivityThread;
import android.content.Context;

public final class OwnContextTest {
    private static void check(boolean b, String message) {
        if (!b) {
            throw new AssertionError(message);
        }
    }

    public static void main(String[] args) {
        ActivityThread.reset();
        Context early = OwnContext.get();
        check(
                early != null && "com.baidu.naviauto".equals(early.getOpPackageName()),
                "early provider context must attribute to Navi, never android");
        Context app = new Context();
        ActivityThread.app = app;
        check(OwnContext.get() == app, "preserve valid current application");
        app.opPackageName = "android";
        check(OwnContext.get() == null, "reject application attribution mismatch");
        ActivityThread.reset();
        android.os.Process.uid = 1000;
        check(OwnContext.get() == null, "reject system uid");
        ActivityThread.reset();
        ActivityThread.name = "other";
        check(OwnContext.get() == null, "reject foreign bound name");
        ActivityThread.reset();
        ActivityThread.bound().context.info.uid = 10131;
        check(OwnContext.get() == null, "reject foreign LoadedApk uid");
        ActivityThread.reset();
        ActivityThread.bound().context.info.packageName = "other";
        check(OwnContext.get() == null, "reject foreign LoadedApk package");
        ActivityThread.reset();
        ActivityThread.bound().context.manager.packages =
                new String[] {"com.baidu.naviauto", "other"};
        check(OwnContext.get() == null, "reject shared uid");
        ActivityThread.reset();
        ActivityThread.bound().context.opPackageName = "android";
        check(OwnContext.get() == null, "reject early attribution mismatch");
        ActivityThread.reset();
        ActivityThread.missingLoaded();
        check(OwnContext.get() == null, "missing binding fails closed");
        System.out.println(
                "PASS early Navi attribution and eight caller/binding/application negatives");
    }
}
