package com.android.server.wm;

import android.graphics.Rect;
import android.os.Build;
import android.os.SystemProperties;

public final class HiboardTopGestureTest {
    static void check(boolean b, String name) {
        if (!b) {
            throw new AssertionError(name);
        }
    }

    static DisplayPolicy fixture() {
        Build.VERSION.SDK_INT = 30;
        SystemProperties.VALUES.put("ro.build.product", "msmnile_gvmq");
        SystemProperties.VALUES.put("ro.build.product_3rdparty", "f6802h");
        DisplayPolicy p = new DisplayPolicy();
        p.status = new WindowState();
        p.status.pkg = "com.android.systemui";
        p.status.attrs.type = 2000;
        p.display.windows.add(new WindowState());

        return p;
    }

    static void run(DisplayPolicy p, WindowState target, boolean original) {
        if (original) {
            DisplayPolicy.access$400(p, target);
        } else {
            HiboardTopGesture.request(p, target);
        }
    }

    public static void main(String[] a) {
        boolean original = a.length > 0 && a[0].equals("--original");
        DisplayPolicy p = fixture();
        WindowState w = p.display.windows.get(0);
        Object attrs = w.attrs, frame = w.frame;

        run(p, p.status, original);
        check(
                p.originalCalls == 0,
                "RED original stock side effect must be suppressed only for visible exact OEM"
                        + " delegate");
        check(
                w.attrs == attrs && w.frame == frame && w.attrs.flags == 131592,
                "OEM window parameters unchanged");

        for (int mode = 0; mode < 30; mode++) {
            p = fixture();
            w = p.display.windows.get(0);
            WindowState target = p.status;

            switch (mode) {
                case 0:
                    Build.VERSION.SDK_INT = 28;
                    break;
                case 1:
                    SystemProperties.VALUES.put("ro.build.product", "foreign");
                    break;
                case 2:
                    SystemProperties.VALUES.remove("ro.build.product_3rdparty");
                    break;
                case 3:
                    p.display.windows.clear();
                    break;
                case 4:
                    w.pkg = "foreign";
                    break;
                case 5:
                    w.uid = 10123;
                    break;
                case 6:
                    w.attrs.type = 2118;
                    break;
                case 7:
                    w.display = 1;
                    break;
                case 8:
                    w.frame.left = 0;
                    break;
                case 9:
                    w.visible = false;
                    break;
                case 10:
                    p.display.windows.add(new WindowState());
                    break;
                case 11:
                    p.gestures.x = 144;
                    break;
                case 12:
                    p.gestures.y = 50;
                    break;
                case 13:
                    w.attrs.title = "SecondHiboardDelegate";
                    break;
                case 14:
                    w.attrs.flags |= 16;
                    break;
                case 15:
                    p.display.id = 1;
                    break;
                case 16:
                    p.display.info.logicalHeight = 1080;
                    break;
                case 17:
                    p.display.info.logicalDensityDpi = 240;
                    break;
                case 18:
                    p.display.broken = true;
                    break;
                case 19:
                    p.gestures.broken = true;
                    break;
                case 20:
                    target = new WindowState();
                    break;
                case 21:
                    w.mHasSurface = false;
                    break;
                case 22:
                    w.mObscured = true;
                    break;
                case 23:
                    p.status.pkg = "com.qinggan.systemui";
                    break;
                case 24:
                    p.status.attrs.type = 2019;
                    break;
                case 25:
                    p.status.display = 1;
                    break;
                case 26:
                    w.touch = new Rect(0, 0, 145, 50);
                    break;
                case 27:
                    p.gestures = null;
                    break;
                case 28:
                    p.display = null;
                    break;
                case 29:
                    w.attrs = null;
                    break;
                default:
                    throw new AssertionError("unknown mode " + mode);
            }

            run(p, target, false);
            check(p.originalCalls == 1, "Original fallback exactly once mode " + mode);
        }

        p = fixture();
        p.display.windows.clear();
        p.originalThrows = true;

        try {
            run(p, p.status, false);
            throw new AssertionError("Original exception expected");
        } catch (IllegalStateException expected) {
            check(p.originalCalls == 1, "Never replay uncertain original side effect");
        }

        System.out.println(
                "GREEN exact delegate owns top gesture; 30 boundary fallbacks + original-error no"
                        + " replay; OEM params unchanged");
    }
}
