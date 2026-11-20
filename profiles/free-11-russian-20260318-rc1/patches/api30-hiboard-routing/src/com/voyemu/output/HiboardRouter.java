package com.voyemu.output;

import android.content.Context;
import android.graphics.Point;
import android.hardware.display.DisplayManager;
import android.os.Handler;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.view.Display;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;

import java.util.ArrayList;
import java.util.IdentityHashMap;

/** Routes only the original Free A11 7.3.1 Hiboard edge, body and dim window tuples. */
public final class HiboardRouter {
    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    private static final IdentityHashMap<View, Entry> ENTRIES = new IdentityHashMap<>();
    private static DisplayManager displays;

    private static final class Entry {
        final View view;
        final Context context;
        final WindowManager original;
        WindowManager.LayoutParams params;
        WindowManager attached;
        int display = -1;

        Entry(WindowManager original, View view, WindowManager.LayoutParams params) {
            this.original = original;
            this.view = view;
            this.params = params;
            Context app = view.getContext().getApplicationContext();
            context = app == null ? view.getContext() : app;
        }
    }

    private HiboardRouter() {}

    static int target(String owner, int type, String title, int memory) {
        if (!"com.qinggan.app.hiboard".equals(owner) || type != 2106) {
            return -1;
        }

        if (memory == 1
                && ("HiboardBar".equals(title)
                        || "HiboardDelegate".equals(title)
                        || "VehicleHiBoardBarNew".equals(title)
                        || "VehicleHiBoardBarNewOTA".equals(title)
                        || "VehicleHiboardDelegate".equals(title)
                        || "DimView".equals(title))) {
            return 0;
        }

        if (memory == 2
                && ("SecondHiboardBar".equals(title)
                        || "SecondHiboardDelegate".equals(title)
                        || "SecondVehicleHiboardBar".equals(title)
                        || "SecondDimView".equals(title))) {
            return 1;
        }

        return -1;
    }

    private static boolean knownTitle(View view, WindowManager.LayoutParams params) {
        CharSequence title = params.getTitle();
        String name = title == null ? null : title.toString();
        String owner = view.getContext().getPackageName();

        return target(owner, params.type, name, 1) >= 0 || target(owner, params.type, name, 2) >= 0;
    }

    private static int target(View view, ViewGroup.LayoutParams params) {
        if (!(params instanceof WindowManager.LayoutParams)) {
            return -1;
        }

        WindowManager.LayoutParams p = (WindowManager.LayoutParams) params;
        CharSequence title = p.getTitle();

        return target(
                view.getContext().getPackageName(),
                p.type,
                title == null ? null : title.toString(),
                p.memoryType);
    }

    private static boolean geometry(Display display) {
        Point size = new Point();

        display.getRealSize(size);
        DisplayMetrics metrics = new DisplayMetrics();
        display.getRealMetrics(metrics);

        return size.x == 1920 && size.y == 720 && metrics.densityDpi == 160;
    }

    private static Display resolve(int slot) {
        if (slot == 0) {
            Display primary = displays.getDisplay(Display.DEFAULT_DISPLAY);

            return primary != null && geometry(primary) ? primary : null;
        }

        Display result = null;

        for (Display display : displays.getDisplays()) {
            String name = display.getName();

            if (display.getDisplayId() == Display.DEFAULT_DISPLAY
                    || (!"Emulator2D".equals(name) && !"Emulator 2D Display".equals(name))
                    || !geometry(display)) {
                continue;
            }

            if (result != null) {
                return null;
            }

            result = display;
        }

        return result;
    }

    private static void onMain(Runnable task) {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            task.run();
        } else if (!MAIN.post(task)) {
            throw new IllegalStateException("Hiboard main Looper stopped");
        }
    }

    private static void detach(Entry entry) {
        if (entry.attached == null) {
            return;
        }

        try {
            entry.attached.removeViewImmediate(entry.view);
        } catch (IllegalArgumentException removedByDisplay) {
            /* A removed display may already have detached its windows. */
        }

        entry.attached = null;
        entry.display = -1;
    }

    private static void reconcile(Entry entry, boolean update) {
        int slot = target(entry.view, entry.params);

        if (slot < 0) {
            detach(entry);

            if (knownTitle(entry.view, entry.params)) {
                return;
            }

            ENTRIES.remove(entry.view);
            entry.original.addView(entry.view, entry.params);

            return;
        }

        Display selected = resolve(slot);

        if (entry.attached != null
                && (selected == null || entry.display != selected.getDisplayId())) {
            detach(entry);
        }

        if (entry.attached != null) {
            if (update) {
                entry.attached.updateViewLayout(entry.view, entry.params);
            }

            return;
        }

        if (selected == null) {
            return;
        }

        WindowManager manager =
                (WindowManager)
                        entry.context
                                .createDisplayContext(selected)
                                .getSystemService(Context.WINDOW_SERVICE);

        if (manager == null) {
            throw new IllegalStateException("Display WindowManager unavailable");
        }

        try {
            manager.addView(entry.view, entry.params);
            entry.attached = manager;
            entry.display = selected.getDisplayId();
        } catch (WindowManager.InvalidDisplayException removedBeforeAttach) {
            /* Retry only on a display callback or the next original window operation. */
        }
    }

    private static void refresh() {
        for (Entry entry : new ArrayList<>(ENTRIES.values())) {
            reconcile(entry, false);
        }
    }

    private static void listen(Context context) {
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
                        refresh();
                    }

                    @Override
                    public void onDisplayChanged(int id) {
                        refresh();
                    }

                    @Override
                    public void onDisplayRemoved(int id) {
                        for (Entry entry : new ArrayList<>(ENTRIES.values())) {
                            if (entry.display == id) {
                                detach(entry);
                            }
                        }

                        refresh();
                    }
                },
                MAIN);
    }

    public static void addView(
            final WindowManager original, final View view, final ViewGroup.LayoutParams params) {
        onMain(() -> {
            Entry entry = ENTRIES.get(view);

            if (entry == null) {
                if (target(view, params) < 0) {
                    original.addView(view, params);

                    return;
                }

                entry = new Entry(original, view, (WindowManager.LayoutParams) params);
                listen(entry.context);
                ENTRIES.put(view, entry);
            } else if (!(params instanceof WindowManager.LayoutParams)) {
                ENTRIES.remove(view);
                detach(entry);
                original.addView(view, params);

                return;
            } else {
                entry.params = (WindowManager.LayoutParams) params;
            }

            reconcile(entry, true);
        });
    }

    public static void updateViewLayout(
            final WindowManager original, final View view, final ViewGroup.LayoutParams params) {
        onMain(() -> {
            Entry entry = ENTRIES.get(view);

            if (entry == null) {
                original.updateViewLayout(view, params);

                return;
            }

            if (!(params instanceof WindowManager.LayoutParams)) {
                ENTRIES.remove(view);
                detach(entry);
                original.addView(view, params);

                return;
            }

            entry.params = (WindowManager.LayoutParams) params;
            reconcile(entry, true);
        });
    }

    private static void remove(
            final WindowManager original, final View view, final boolean immediate) {
        onMain(() -> {
            Entry entry = ENTRIES.remove(view);
            WindowManager manager = entry == null ? original : entry.attached;

            if (manager == null) {
                return;
            }

            if (immediate) {
                manager.removeViewImmediate(view);
            } else {
                manager.removeView(view);
            }
        });
    }

    public static void removeView(WindowManager original, View view) {
        remove(original, view, false);
    }

    public static void removeViewImmediate(WindowManager original, View view) {
        remove(original, view, true);
    }
}
