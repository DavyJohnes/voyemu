package com.voyemu.output;

import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Display;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewManager;
import android.view.WindowManager;

import java.util.ArrayList;
import java.util.IdentityHashMap;

/** Emulator-only lifecycle adapter for the Launcher's OEM service windows. */
public final class WindowRouter {
    private static final String TAG = "WindowRouter";
    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    // Entry holds a strong ref to the View key, so WeakHashMap would not help.
    // Entries are cleared by explicit removeView/removeViewImmediate (all patched)
    // or by process death — bounded by the Launcher process lifetime.
    private static final IdentityHashMap<View, Entry> ENTRIES = new IdentityHashMap<>();
    private static DisplayManager displays;

    // The emulator recreates the virtual display on every provider restart,
    // and its id keeps shifting (1→2→...→14→1). Resolve the secondary display
    // by its stable name (Emulator2D) instead of hardcoding display 1; see
    // Entry.resolveDisplayId().

    private static final class Entry {
        final View view;
        final Context context;
        WindowManager.LayoutParams params;
        // Logical slot: 0 = main built-in display, 1 = secondary virtual display.
        final int slot;
        WindowManager attached;
        // Display id the window is actually attached to; the live display
        // lookup cannot answer this after the display is already removed.
        int attachedDisplayId = -1;
        int attempts;

        Entry(View view, WindowManager.LayoutParams params, int slot) {
            this.view = view;
            Context application = view.getContext().getApplicationContext();
            this.context = application == null ? view.getContext() : application;
            this.params = params;
            this.slot = slot;
        }

        int resolveDisplayId() {
            if (slot == 0) {
                return Display.DEFAULT_DISPLAY;
            }

            Display[] live = displays.getDisplays();

            for (Display display : live) {
                String unique = display.getName() != null ? display.getName() : "";

                if (display.getDisplayId() != Display.DEFAULT_DISPLAY
                        && unique.contains("Emulator2D")) {
                    return display.getDisplayId();
                }
            }

            return -1;
        }
    }

    private WindowRouter() {}

    static int target(String owner, int type, String title) {
        if ("com.qinggan.systemui".equals(owner) && (type == 2118 || type == 2026)) {
            if ("StatusBar1".equals(title)) {
                return 0;
            }

            if ("StatusBar2".equals(title)) {
                return 1;
            }

            return -1;
        }

        if (!"com.qinggan.app.launcher".equals(owner)
                || (type != 2106 && type != 2110)
                || title == null) {
            return -1;
        }

        return launcherTarget(title);
    }

    private static int launcherTarget(String title) {
        if (title.equals("SecondScreenNavigationBar")
                || title.equals("SecondHiboardDelegate")
                || title.equals("SecondHiboardBar")
                || title.equals("SecondVehicleHiboardBar")
                || title.equals("SecondDimView")) {
            return 1;
        }

        if (title.equals("MainScreenNavigationBar")
                || title.equals("HiboardDelegate")
                || title.equals("VehicleHiboardDelegate")
                || title.equals("HiboardBar")
                || title.equals("VehicleHiBoardBarNew")
                || title.equals("VehicleHiboardAboveWindow")
                || title.equals("changeThemeLoading")
                || title.equals("DimView")
                || title.equals("CardSelectedPop")) {
            return 0;
        }

        return -1;
    }

    private static int target(View view, ViewGroup.LayoutParams params) {
        if (!(params instanceof WindowManager.LayoutParams)) {
            return -1;
        }

        WindowManager.LayoutParams window = (WindowManager.LayoutParams) params;
        CharSequence title = window.getTitle();

        // OEM AllApp popups share a title; the inflated view retains its logical display tag.
        if ("com.qinggan.app.launcher".equals(view.getContext().getPackageName())
                && window.type == 2106
                && "AllApp".contentEquals(title == null ? "" : title)
                && "com.qinggan.app.launcher.widget.AllAppBarView"
                        .equals(view.getClass().getName())) {
            Object tag = view.getTag();

            if ("Second".equals(tag)) {
                return 1;
            }

            if ("".equals(tag)) {
                return 0;
            }
        }

        return target(
                view.getContext().getPackageName(),
                window.type,
                title == null ? null : title.toString());
    }

    // AOSP puts unrecognized vendor types on the application layer, below the
    // Ivoka PHONE window. Adapt only the known OEM panel windows; retain their
    // original params for routing and copy every layout/flag unchanged.
    private static ViewGroup.LayoutParams platformParams(View view, ViewGroup.LayoutParams params) {
        if (target(view, params) < 0) {
            return params;
        }

        WindowManager.LayoutParams original = (WindowManager.LayoutParams) params;

        if (original.type != 2106) {
            return params;
        }

        WindowManager.LayoutParams adapted = new WindowManager.LayoutParams();
        adapted.copyFrom(original);
        adapted.type = WindowManager.LayoutParams.TYPE_SYSTEM_DIALOG;

        return adapted;
    }

    public static void addView(
            final ViewManager original, final View view, final ViewGroup.LayoutParams params) {
        final int displayId = target(view, params);

        if (displayId < 0) {
            original.addView(view, params);

            return;
        }

        onMain(
                new Runnable() {
                    @Override
                    public void run() {
                        Entry existing = ENTRIES.get(view);

                        if (existing != null) {
                            existing.params = (WindowManager.LayoutParams) params;

                            if (existing.attached != null) {
                                existing.attached.updateViewLayout(
                                        view, platformParams(view, params));
                            } else {
                                attach(existing);
                            }

                            return;
                        }

                        Entry entry =
                                new Entry(view, (WindowManager.LayoutParams) params, displayId);
                        ensureListener(entry.context);
                        ENTRIES.put(view, entry);
                        attach(entry);
                    }
                });
    }

    public static void updateViewLayout(
            final ViewManager original, final View view, final ViewGroup.LayoutParams params) {
        if (target(view, params) < 0) {
            original.updateViewLayout(view, params);

            return;
        }

        onMain(
                new Runnable() {
                    @Override
                    public void run() {
                        Entry entry = ENTRIES.get(view);

                        if (entry == null) {
                            original.updateViewLayout(view, params);

                            return;
                        }

                        entry.params = (WindowManager.LayoutParams) params;

                        if (entry.attached != null) {
                            entry.attached.updateViewLayout(view, platformParams(view, params));
                        }
                    }
                });
    }

    public static void removeView(final ViewManager original, final View view) {
        remove(original, view, false);
    }

    public static void removeViewImmediate(final WindowManager original, final View view) {
        remove(original, view, true);
    }

    private static void remove(
            final ViewManager original, final View view, final boolean immediate) {
        onMain(
                new Runnable() {
                    @Override
                    public void run() {
                        Entry entry = ENTRIES.remove(view);

                        if (entry == null) {
                            if (immediate) {
                                ((WindowManager) original).removeViewImmediate(view);
                            } else {
                                original.removeView(view);
                            }

                            return;
                        }

                        if (entry.attached != null) {
                            if (immediate) {
                                entry.attached.removeViewImmediate(view);
                            } else {
                                entry.attached.removeView(view);
                            }

                            entry.attached = null;
                            entry.attachedDisplayId = -1;
                        }

                        Log.i(
                                TAG,
                                "removed title=" + entry.params.getTitle() + " slot=" + entry.slot);
                    }
                });
    }

    private static void onMain(Runnable operation) {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            operation.run();
        } else if (!MAIN.post(operation)) {
            throw new IllegalStateException("launcher main Looper stopped");
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
                        refresh(id);
                    }

                    @Override
                    public void onDisplayChanged(int id) {
                        refresh(id);
                    }

                    @Override
                    public void onDisplayRemoved(int id) {
                        for (Entry entry : new ArrayList<>(ENTRIES.values())) {
                            if (entry.attached == null) {
                                continue;
                            }

                            // Compare with the id recorded at attach time: resolving
                            // against the live displays can no longer find the removed
                            // one and would skip the cleanup below.
                            if (entry.attachedDisplayId != id) {
                                continue;
                            }

                            try {
                                entry.attached.removeViewImmediate(entry.view);
                            } catch (IllegalArgumentException alreadyRemoved) {
                                Log.i(
                                        TAG,
                                        "display removal already detached title="
                                                + entry.params.getTitle());
                            }

                            entry.attached = null;
                            entry.attachedDisplayId = -1;
                            entry.attempts = 0;
                            Log.i(
                                    TAG,
                                    "waiting after display removal title="
                                            + entry.params.getTitle());
                        }
                    }
                },
                MAIN);
    }

    private static void refresh(int displayId) {
        for (Entry entry : new ArrayList<>(ENTRIES.values())) {
            if (entry.resolveDisplayId() == displayId && entry.attached == null) {
                entry.attempts = 0;
                attach(entry);
            }

            // A secondary entry may still be waiting for the previous display
            // id; any added/changed display is a chance to re-resolve it.
            if (entry.attached == null) {
                entry.attempts = 0;
                attach(entry);
            }
        }
    }

    // Restore the edge handle after a late navigation window without moving
    // a full-size popup or any window belonging to the other display.
    private static void raiseEdgeDelegate(Entry navigation) {
        String title = String.valueOf(navigation.params.getTitle());
        String edge =
                "MainScreenNavigationBar".equals(title)
                        ? "VehicleHiboardDelegate"
                        : "SecondScreenNavigationBar".equals(title)
                                ? "SecondVehicleHiboardBar"
                                : null;

        if (edge == null || navigation.params.width <= 0) {
            return;
        }

        for (Entry entry : new ArrayList<>(ENTRIES.values())) {
            if (entry.attached == null
                    || entry.attachedDisplayId != navigation.attachedDisplayId
                    || !edge.equals(String.valueOf(entry.params.getTitle()))
                    || entry.params.width <= 0
                    || entry.params.width >= navigation.params.width) {
                continue;
            }

            entry.attached.removeViewImmediate(entry.view);
            entry.attached = null;
            entry.attachedDisplayId = -1;
            attach(entry);
        }
    }

    private static void attach(final Entry entry) {
        if (ENTRIES.get(entry.view) != entry || entry.attached != null) {
            return;
        }

        int displayId = entry.resolveDisplayId();
        Display display = displayId >= 0 ? displays.getDisplay(displayId) : null;

        if (display == null || !display.isValid()) {
            Log.i(TAG, "deferred title=" + entry.params.getTitle() + " slot=" + entry.slot);

            return;
        }

        try {
            Context context = entry.context.createDisplayContext(display);
            WindowManager manager =
                    (WindowManager) context.getSystemService(Context.WINDOW_SERVICE);
            manager.addView(entry.view, platformParams(entry.view, entry.params));
            entry.attached = manager;
            entry.attachedDisplayId = displayId;
            entry.attempts = 0;
            Log.i(TAG, "attached title=" + entry.params.getTitle() + " display=" + displayId);
            raiseEdgeDelegate(entry);
        } catch (WindowManager.InvalidDisplayException failure) {
            if (++entry.attempts <= 5) {
                MAIN.postDelayed(
                        new Runnable() {
                            @Override
                            public void run() {
                                attach(entry);
                            }
                        },
                        200);
            } else {
                Log.e(TAG, "display admission failed title=" + entry.params.getTitle(), failure);
            }
        }
    }
}
