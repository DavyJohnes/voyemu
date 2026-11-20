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

/** Display-specific routing for the two original Free A11 7.3.1 OEM status-bar windows only. */
public final class StatusBarRouter {
    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    private static final IdentityHashMap<View, Entry> ENTRIES = new IdentityHashMap<>();
    private static DisplayManager displays;

    private static final class Entry {
        final View view;
        final Context context;
        final int slot;
        WindowManager.LayoutParams params;
        WindowManager attached;
        int display = -1;

        Entry(View view, WindowManager.LayoutParams params, int slot) {
            this.view = view;
            Context app = view.getContext().getApplicationContext();
            this.context = app == null ? view.getContext() : app;
            this.params = params;
            this.slot = slot;
        }
    }

    private StatusBarRouter() {}

    static int target(String owner, int type, String title) {
        if (!"com.qinggan.systemui".equals(owner) || type != 2118) {
            return -1;
        }

        if ("StatusBar1".equals(title)) {
            return 0;
        }

        if ("StatusBar2".equals(title)) {
            return 1;
        }

        return -1;
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
                title == null ? null : title.toString());
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
            throw new IllegalStateException("SystemUI main Looper stopped");
        }
    }

    private static void detach(Entry entry) {
        if (entry.attached != null) {
            try {
                entry.attached.removeViewImmediate(entry.view);
            } catch (IllegalArgumentException removedByDisplay) {
                /* WindowManager already detached a removed display. */
            }

            entry.attached = null;
            entry.display = -1;
        }
    }

    private static void attach(Entry entry) {
        Display selected = resolve(entry.slot);

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
            /* Wait for the actual display callback; never attach to primary as fallback. */
        }
    }

    private static void refresh() {
        for (Entry entry : new ArrayList<>(ENTRIES.values())) {
            Display selected = resolve(entry.slot);

            if (entry.attached != null
                    && (selected == null || entry.display != selected.getDisplayId())) {
                detach(entry);
            }

            if (entry.attached == null) {
                attach(entry);
            }
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
        final int slot = target(view, params);

        if (slot < 0) {
            original.addView(view, params);

            return;
        }

        onMain(() -> {
            Entry entry = ENTRIES.get(view);

            if (entry != null) {
                entry.params = (WindowManager.LayoutParams) params;

                if (entry.attached != null) {
                    entry.attached.updateViewLayout(view, params);
                } else {
                    attach(entry);
                }

                return;
            }

            entry = new Entry(view, (WindowManager.LayoutParams) params, slot);
            listen(entry.context);
            ENTRIES.put(view, entry);
            attach(entry);
        });
    }

    public static void removeView(final WindowManager original, final View view) {
        onMain(() -> {
            Entry entry = ENTRIES.remove(view);

            if (entry == null) {
                original.removeView(view);

                return;
            }

            if (entry.attached != null) {
                entry.attached.removeView(view);
                entry.attached = null;
                entry.display = -1;
            }
        });
    }
}
