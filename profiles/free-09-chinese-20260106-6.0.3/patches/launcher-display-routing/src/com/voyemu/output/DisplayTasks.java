package com.voyemu.output;

import android.app.ActivityManager;
import android.content.ComponentName;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.util.Log;
import android.view.Display;

import java.util.List;

/** Android 9 stack lookup replacing the proprietary getDpyTopAppInfo service. */
public final class DisplayTasks {
    private static final String TAG = "DisplayTasks";

    private DisplayTasks() {}

    public static String getTopAppInfo(Context context, int slot, int format) {
        if ((slot != 0 && slot != 1) || (format != 1 && format != 2 && format != 4)) {
            return null;
        }

        int displayId = resolveDisplayId(context, slot);

        if (displayId < 0) {
            return null;
        }

        try {
            // These hidden APIs exist in the pinned Android 9 image. The launcher is platform
            // signed.
            Object service = ActivityManager.class.getMethod("getService").invoke(null);
            Object result =
                    Class.forName("android.app.IActivityManager")
                            .getMethod("getAllStackInfos")
                            .invoke(service);

            if (!(result instanceof List<?>)) {
                return null;
            }

            ComponentName top = topActivity((List<?>) result, displayId);

            return top == null
                    ? null
                    : format == 1
                            ? top.getPackageName()
                            : format == 2 ? top.getClassName() : top.flattenToString();
        } catch (ReflectiveOperationException | SecurityException exception) {
            Log.w(TAG, "Cannot resolve foreground activity", exception);

            return null;
        }
    }

    private static int resolveDisplayId(Context context, int slot) {
        int displayId = Display.DEFAULT_DISPLAY;

        if (slot == 1) {
            displayId = -1;
            DisplayManager manager =
                    (DisplayManager) context.getSystemService(Context.DISPLAY_SERVICE);

            if (manager == null) {
                return -1;
            }

            for (Display display : manager.getDisplays()) {
                if (display.getDisplayId() != Display.DEFAULT_DISPLAY
                        && display.getName() != null
                        && display.getName().contains("Emulator2D")) {
                    displayId = display.getDisplayId();
                    break;
                }
            }

            if (displayId < 0) {
                return -1;
            }
        }

        return displayId;
    }

    private static ComponentName topActivity(List<?> stacks, int displayId)
            throws ReflectiveOperationException {
        ComponentName top = null;
        int position = -1;

        for (Object stack : stacks) {
            Class<?> type = stack.getClass();

            if (type.getField("displayId").getInt(stack) != displayId
                    || !type.getField("visible").getBoolean(stack)) {
                continue;
            }

            int candidatePosition = type.getField("position").getInt(stack);
            Object candidate = type.getField("topActivity").get(stack);

            if (candidatePosition > position && candidate instanceof ComponentName) {
                top = (ComponentName) candidate;
                position = candidatePosition;
            }
        }

        return top;
    }
}
