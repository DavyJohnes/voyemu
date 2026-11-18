package com.voyemu.output;

import android.app.ActivityOptions;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ResolveInfo;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Display;

import java.util.ArrayList;
import java.util.List;

/**
 * Emulator-only launch router: the OEM Launcher decides the target display before the emulator's
 * virtual secondary display exists, so second-screen activities (SecondMainActivity and other
 * com.qinggan.secondlauncher.*) fall through to display 0 and cover the main home screen. Both
 * ActivityAnimUtils.startActivityByAnim overloads are patched to call launch() instead of doing
 * setLaunchDisplayId + a direct startActivity.
 */
public final class LaunchRouter {
    private static final String TAG = "LaunchRouter";
    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    // Keep the caller context only during the five-minute guest boot window.
    private static final List<Pending> PENDING = new ArrayList<Pending>();
    private static DisplayManager displays;

    private static final class Pending implements Runnable {
        final Context context;
        final Intent intent;
        final ActivityOptions options;

        Pending(Context context, Intent intent, ActivityOptions options) {
            this.context = context;
            this.intent = intent;
            this.options = options;
        }

        @Override
        public void run() {
            if (PENDING.remove(this)) {
                Log.i(TAG, "expired deferred " + name(intent));
            }
        }
    }

    private LaunchRouter() {}

    public static void launch(
            final Context context,
            final Intent intent,
            final ActivityOptions options,
            final int requested) {
        if (intent == null || context.getPackageManager().resolveActivity(intent, 0) == null) {
            return;
        }

        onMain(
                new Runnable() {
                    @Override
                    public void run() {
                        if (!isSecondary(intent, requested)) {
                            start(context, intent, options, Display.DEFAULT_DISPLAY, requested);

                            return;
                        }

                        // The DisplayManager must exist before the display lookup:
                        // on the first call it is still null and secondary() would
                        // defer the launch even when the display already exists.
                        ensureListener(context.getApplicationContext());
                        Display secondary = secondary();

                        if (secondary != null) {
                            start(context, intent, options, secondary.getDisplayId(), requested);
                        } else {
                            Pending pending = new Pending(context, intent, options);
                            PENDING.add(pending);
                            MAIN.postDelayed(pending, 300_000);
                            Log.i(TAG, "deferred " + name(intent) + " (no secondary display yet)");
                        }
                    }
                });
    }

    // A launch belongs to the second screen when the caller asked for a non-zero
    // display, or when the intent addresses the OEM second-launcher package.
    private static boolean isSecondary(Intent intent, int requested) {
        if (requested != 0) {
            return true;
        }

        ComponentName component = intent.getComponent();

        return component != null
                && "com.qinggan.app.launcher".equals(component.getPackageName())
                && component.getClassName().contains("secondlauncher");
    }

    private static Display secondary() {
        if (displays == null) {
            return null;
        }

        // The emulator recreates the virtual display on every provider restart
        // and its id keeps shifting; resolve by the stable name instead.
        for (Display display : displays.getDisplays()) {
            if (display.getDisplayId() == Display.DEFAULT_DISPLAY) {
                continue;
            }

            String name = display.getName();

            if (name != null && name.contains("Emulator2D")) {
                return display;
            }
        }

        return null;
    }

    private static void start(
            Context context, Intent intent, ActivityOptions options, int displayId, int requested) {
        // On A9 a CATEGORY_HOME intent is classified as a home-type activity and
        // forced into the display-0 home stack, silently overriding
        // setLaunchDisplayId. The OEM goHome helpers add the category below
        // SDK 30, so drop it for second-launcher launches to let the activity
        // open as standard on the secondary display.
        if (displayId != Display.DEFAULT_DISPLAY) {
            intent.removeCategory(Intent.CATEGORY_HOME);
        }

        options.setLaunchDisplayId(displayId);
        Bundle bundle = options.toBundle();

        try {
            context.startActivity(intent, bundle);
        } catch (RuntimeException failure) {
            // checkstyle-allow IllegalCatch: Any rejected Android activity launch must leave the
            // launcher process alive.
            // system_server can reject a launch for many reasons (home->standard
            // activity type change, missing permission, bad display). The router
            // must never take the launcher process down with it.
            Log.w(TAG, "launch failed " + name(intent) + " display=" + displayId, failure);

            return;
        }

        // AOSP does not emit the OEM top-activity broadcast. Keep the OEM
        // navigation selection/animation in sync with successful routed launches.
        // The receiver uses logical screen slots, not recreated Android display IDs.
        ComponentName component = intent.getComponent();

        if (component == null) {
            ResolveInfo resolved = context.getPackageManager().resolveActivity(intent, 0);

            if (resolved != null && resolved.activityInfo != null) {
                component =
                        new ComponentName(
                                resolved.activityInfo.packageName, resolved.activityInfo.name);
            }
        }

        if (component != null) {
            Intent changed = new Intent("android.intent.action.TOP_ACTIVITY_CHANGED");
            changed.setPackage("com.qinggan.app.launcher");
            changed.putExtra("displayId", displayId == Display.DEFAULT_DISPLAY ? 0 : 1);
            changed.putExtra(Intent.EXTRA_COMPONENT_NAME, component.flattenToString());

            try {
                context.sendBroadcast(changed);
            } catch (RuntimeException failure) {
                // checkstyle-allow IllegalCatch: A rejected Android navigation broadcast must leave
                // the launcher process alive.
                Log.w(TAG, "navigation update failed " + name(intent), failure);
            }
        }

        if (displayId != requested) {
            Log.i(
                    TAG,
                    "routed " + name(intent) + " requested=" + requested + " display=" + displayId);
        }
    }

    private static void ensureListener(Context context) {
        if (displays != null) {
            return;
        }

        displays = (DisplayManager) context.getSystemService(Context.DISPLAY_SERVICE);

        if (displays == null) {
            throw new IllegalStateException("DisplayManager unavailable");
        }

        displays.registerDisplayListener(
                new DisplayManager.DisplayListener() {
                    @Override
                    public void onDisplayAdded(int id) {
                        flush();
                    }

                    @Override
                    public void onDisplayChanged(int id) {
                        flush();
                    }

                    @Override
                    public void onDisplayRemoved(int id) {}
                },
                MAIN);
        // The deferred launch might have raced display creation: the listener
        // registers after onDisplayAdded was already delivered.
        flush();
    }

    private static void flush() {
        if (PENDING.isEmpty() || secondary() == null) {
            return;
        }

        Display secondary = secondary();

        for (Pending pending : new ArrayList<Pending>(PENDING)) {
            MAIN.removeCallbacks(pending);
            start(pending.context, pending.intent, pending.options, secondary.getDisplayId(), -1);
        }

        PENDING.clear();
        Log.i(TAG, "flushed pending launches onto display=" + secondary.getDisplayId());
    }

    private static void onMain(Runnable operation) {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            operation.run();
        } else if (!MAIN.post(operation)) {
            throw new IllegalStateException("launcher main Looper stopped");
        }
    }

    private static String name(Intent intent) {
        ComponentName component = intent.getComponent();

        return component == null ? intent.toString() : component.flattenToShortString();
    }
}
