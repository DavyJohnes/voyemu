package com.android.server.wm;

import android.graphics.Rect;

public final class HiboardTopGestureOcclusionTest {
    static void check(boolean b, String message) {
        if (!b) {
            throw new AssertionError(message);
        }
    }

    static WindowState overlay() {
        WindowState w = new WindowState();

        w.pkg = "foreign.overlay";
        w.uid = 10100;
        w.attrs.type = 2038;
        w.attrs.flags = 8;
        w.frame = new Rect(900, 0, 1100, 30);
        w.touch = new Rect(900, 0, 1100, 30);
        w.mObscured = false;

        return w;
    }

    public static void main(String[] a) {
        int count = 0;

        for (int mode = 0; mode < 9; mode++) {
            DisplayPolicy p = HiboardTopGestureTest.fixture();
            WindowState delegate = p.display.windows.get(0), w = overlay();
            int expected = 0;
            String name;

            switch (mode) {
                case 0:
                    name = "higherPartial";
                    p.display.windows.add(0, w);
                    expected = 1;
                    break;
                case 1:
                    name = "higherTransparent";
                    w.attrs.alpha = 0.3f;
                    w.attrs.format = -3;
                    p.display.windows.add(0, w);
                    expected = 1;
                    break;
                case 2:
                    name = "lowerPartial";
                    p.display.windows.add(w);
                    break;
                case 3:
                    name = "higherOutsideDown";
                    w.frame = new Rect(1000, 0, 1100, 30);
                    w.touch = w.frame;
                    p.display.windows.add(0, w);
                    break;
                case 4:
                    name = "higherNotTouchable";
                    w.attrs.flags |= 16;
                    p.display.windows.add(0, w);
                    break;
                case 5:
                    name = "higherHidden";
                    w.visible = false;
                    p.display.windows.add(0, w);
                    break;
                case 6:
                    name = "higherModalOutsideCustomTouch";
                    w.attrs.flags = 0;
                    w.touch = new Rect(1000, 0, 1100, 30);
                    p.display.windows.add(0, w);
                    expected = 1;
                    break;
                case 7:
                    name = "higherNonModalOutsideCustomTouch";
                    w.attrs.flags = 8;
                    w.touch = new Rect(1000, 0, 1100, 30);
                    p.display.windows.add(0, w);
                    break;
                default:
                    name = "higherClippedVisibleBounds";
                    w.visibleBounds = new Rect(1000, 0, 1100, 30);
                    p.display.windows.add(0, w);
                    break;
            }

            HiboardTopGesture.request(p, p.status);
            check(
                    p.originalCalls == expected,
                    name + ": expected originalCalls=" + expected + " actual=" + p.originalCalls);
            check(
                    !delegate.mObscured,
                    "Partial/transparent windows leave delegate mObscured false");
            count++;

            if (a.length > 0 && a[0].equals("--occlusion-red")) {
                break;
            }
        }

        System.out.println(
                "GREEN "
                        + count
                        + " z-order/transparent/partial/modal/clipped cases; original only for"
                        + " actual higher touch recipient");
    }
}
