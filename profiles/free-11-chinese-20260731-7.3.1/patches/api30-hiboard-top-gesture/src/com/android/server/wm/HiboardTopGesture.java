package com.android.server.wm;

import android.graphics.Rect;
import android.graphics.Region;
import android.os.Build;
import android.os.SystemProperties;
import android.view.DisplayInfo;
import android.view.WindowManager;

import java.util.function.Predicate;

/** Only the Free A11 primary OEM top-edge delegate replaces the stock transient-bar effect. */
public final class HiboardTopGesture {
    private HiboardTopGesture() {}

    public static void request(DisplayPolicy policy, WindowState target) {
        boolean owned = false;

        try {
            owned = owns(policy, target);
        } catch (RuntimeException | LinkageError uncertain) {
            // checkstyle-allow IllegalCatch: preserve OEM policy on any runtime uncertainty.
        }

        if (!owned) {
            DisplayPolicy.access$400(policy, target);
        }
    }

    // Compares identity on purpose: the same Binder, token or instance, never an equal one.
    @SuppressWarnings("ReferenceEquality")
    private static boolean owns(DisplayPolicy policy, WindowState target) {
        if (Build.VERSION.SDK_INT != 30
                || !"msmnile_gvmq".equals(SystemProperties.get("ro.build.product"))
                || !"f6802h".equals(SystemProperties.get("ro.build.product_3rdparty"))
                || policy == null
                || target == null
                || target != DisplayPolicy.access$200(policy)
                || target.getDisplayId() != 0
                || target.getAttrs().type != 2000
                || !"com.android.systemui".equals(target.getOwningPackage())) {
            return false;
        }

        DisplayContent display = DisplayPolicy.access$900(policy);
        SystemGesturesPointerEventListener gestures = DisplayPolicy.access$1100(policy);

        if (display == null || gestures == null || display.getDisplayId() != 0) {
            return false;
        }

        DisplayInfo info = display.getDisplayInfo();

        if (info == null
                || info.logicalWidth != 1920
                || info.logicalHeight != 720
                || info.logicalDensityDpi != 160) {
            return false;
        }

        Candidate candidate = new Candidate(gestures);
        display.getWindow(candidate);

        return candidate.count == 1 && candidate.firstTouchRecipient == candidate.delegate;
    }

    private static final class Candidate implements Predicate<WindowState> {
        private final SystemGesturesPointerEventListener gestures;
        int count;
        WindowState firstTouchRecipient, delegate;

        Candidate(SystemGesturesPointerEventListener gestures) {
            this.gestures = gestures;
        }

        private void trackTouchRecipient(WindowState window) {
            if (firstTouchRecipient != null
                    || window == null
                    || window.getDisplayId() != 0
                    || !window.isVisibleLw()) {
                return;
            }

            WindowManager.LayoutParams params = window.getAttrs();

            if (params == null) {
                throw new IllegalStateException("Unknown window attributes");
            }

            if ((params.flags & WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE) != 0) {
                return;
            }

            Rect bounds = new Rect();
            window.getVisibleBounds(bounds);
            Region visible = new Region();
            visible.set(bounds);

            if (!gestures.currentGestureStartedInRegion(visible)) {
                return;
            }

            Region touch = new Region();
            window.getTouchableRegion(touch);
            int touchFlags =
                    params.flags
                            & (WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                                    | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL);

            if (touchFlags == 0 || gestures.currentGestureStartedInRegion(touch)) {
                firstTouchRecipient = window;
            }
        }

        @Override
        public boolean test(WindowState window) {
            // The platform getWindow traversal is top to bottom, exactly as
            // getTouchableWinAtPointLocked.
            trackTouchRecipient(window);

            if (window == null
                    || window.getOwningUid() != 1000
                    || window.getDisplayId() != 0
                    || !"com.qinggan.app.hiboard".equals(window.getOwningPackage())
                    || !window.mHasSurface
                    || window.mObscured
                    || !window.isVisibleLw()) {
                return false;
            }

            WindowManager.LayoutParams params = window.getAttrs();

            if (params == null
                    || params.type != 2106
                    || !"HiboardDelegate".contentEquals(params.getTitle())
                    || (params.flags & WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE) != 0) {
                return false;
            }

            Rect frame = window.getFrameLw();

            if (frame == null
                    || frame.left != 145
                    || frame.top != 0
                    || frame.right != 1920
                    || frame.bottom != 50) {
                return false;
            }

            Region touch = new Region();
            window.getTouchableRegion(touch);
            touch.op(frame, Region.Op.INTERSECT);

            if (gestures.currentGestureStartedInRegion(touch)) {
                count++;
                delegate = window;
            }

            return false; // Visit the full current inventory; duplicates keep the original policy.
        }
    }
}
