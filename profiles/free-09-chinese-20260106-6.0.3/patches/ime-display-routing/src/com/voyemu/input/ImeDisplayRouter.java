package com.voyemu.input;

import android.app.ActivityManager;
import android.app.Dialog;
import android.content.ComponentName;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.inputmethodservice.InputMethodService;
import android.os.IBinder;
import android.util.Log;
import android.view.Display;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;

import java.lang.reflect.Field;
import java.util.IdentityHashMap;

/** Android 9 binds IME tokens to display 0; route its keyboard to the focused editor display. */
public final class ImeDisplayRouter {
    private static final IdentityHashMap<InputMethodService, State> STATES =
            new IdentityHashMap<InputMethodService, State>();

    private static final class State {
        final WindowManager primary;
        final int type;
        final int flags;
        final IBinder token;
        WindowManager manager;
        int display;

        State(WindowManager primary, WindowManager.LayoutParams attributes) {
            this.primary = primary;
            manager = primary;
            type = attributes.type;
            flags = attributes.flags;
            token = attributes.token;
        }
    }

    private ImeDisplayRouter() {}

    public static void destroy(InputMethodService service) {
        STATES.remove(service);
    }

    private static int editorDisplay(EditorInfo info) {
        if (info == null || info.packageName == null) {
            return 0;
        }

        try {
            Object activityManager = ActivityManager.class.getMethod("getService").invoke(null);
            Object stack =
                    Class.forName("android.app.IActivityManager")
                            .getMethod("getFocusedStackInfo")
                            .invoke(activityManager);

            if (stack == null) {
                return 0;
            }

            Object top = stack.getClass().getField("topActivity").get(stack);

            if (!(top instanceof ComponentName)
                    || !info.packageName.equals(((ComponentName) top).getPackageName())) {
                return 0;
            }

            return stack.getClass().getField("displayId").getInt(stack);
        } catch (ReflectiveOperationException | SecurityException failure) {
            Log.w("VoyemuImeDisplay", "Cannot resolve editor display", failure);

            return 0;
        }
    }

    public static void route(InputMethodService service, EditorInfo info) {
        int requested = editorDisplay(info);
        State state = STATES.get(service);

        if (requested == 0 && state == null) {
            return;
        }

        DisplayManager displays =
                (DisplayManager) service.getSystemService(Context.DISPLAY_SERVICE);
        Display target = displays.getDisplay(requested);

        if (target == null || (requested != 0 && !target.getName().contains("Emulator2D"))) {
            requested = 0;
        }

        if (state != null && state.display == requested) {
            return;
        }

        if (requested == 0 && state == null) {
            return;
        }

        try {
            routeWindow(service, state, requested, target);
        } catch (ReflectiveOperationException failure) {
            Log.w("VoyemuImeDisplay", "Cannot route keyboard window", failure);
        }
    }

    private static void routeWindow(
            InputMethodService service, State state, int requested, Display target)
            throws ReflectiveOperationException {
        Dialog dialog = service.getWindow();
        Window window = dialog.getWindow();
        Field dialogManager = Dialog.class.getDeclaredField("mWindowManager");
        Field windowManager = Window.class.getDeclaredField("mWindowManager");

        dialogManager.setAccessible(true);
        windowManager.setAccessible(true);
        WindowManager.LayoutParams attributes = window.getAttributes();

        if (state == null) {
            state = new State((WindowManager) dialogManager.get(dialog), attributes);
            STATES.put(service, state);
        }

        WindowManager manager =
                requested == 0
                        ? state.primary
                        : (WindowManager)
                                service.createDisplayContext(target)
                                        .getSystemService(Context.WINDOW_SERVICE);
        View decor = window.getDecorView();
        // The root can already be registered before its first attachment callback.
        boolean attached = decor.getParent() != null;

        if (attached) {
            state.manager.removeViewImmediate(decor);
        }

        attributes.type =
                requested == 0 ? state.type : WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY;
        attributes.token = requested == 0 ? state.token : null;
        attributes.flags =
                requested == 0
                        ? state.flags
                        : state.flags | WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
        dialogManager.set(dialog, manager);
        windowManager.set(window, manager);

        if (attached) {
            manager.addView(decor, attributes);
        }

        state.manager = manager;
        state.display = requested;
        Log.i("VoyemuImeDisplay", "keyboard display=" + requested);
    }
}
