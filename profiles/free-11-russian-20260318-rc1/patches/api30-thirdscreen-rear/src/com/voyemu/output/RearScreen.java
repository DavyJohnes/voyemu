package com.voyemu.output;

import android.app.ActivityOptions;
import android.app.ActivityThread;
import android.app.IOnTouchListener;
import android.content.Context;
import android.content.Intent;
import android.hardware.display.DisplayManager;
import android.hardware.input.InputManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.RemoteException;
import android.util.Log;
import android.view.Display;
import android.view.InputChannel;
import android.view.InputEvent;
import android.view.InputEventReceiver;
import android.view.InputMonitor;
import android.view.MotionEvent;

/**
 * Starts the home of the OEM rear screen app on the rear display: the display of the emulator
 * multidisplay provider with the rear unique ID. The OEM start asks for display 3, but display
 * numbers shift when the provider restarts, so the home starts on the rear display each time the
 * provider adds it, and the OEM options with display 3 are dropped. The touches of the rear display
 * reach the OEM touch listener of the app the same way.
 */
public final class RearScreen {
    static final String TAG = "VoyemuRearScreen";
    static final String UNIQUE_ID = "virtual:com.android.emulator.multidisplay:1234563";

    private RearScreen() {}

    public static void startActivity(Context context, Intent intent, Bundle options) {
        final DisplayManager displays = context.getSystemService(DisplayManager.class);

        displays.registerDisplayListener(
                new DisplayManager.DisplayListener() {
                    @Override
                    public void onDisplayAdded(int id) {
                        start(context, displays, intent, id);
                    }

                    @Override
                    public void onDisplayChanged(int id) {}

                    @Override
                    public void onDisplayRemoved(int id) {}
                },
                new Handler(Looper.getMainLooper()));

        for (Display display : displays.getDisplays()) {
            start(context, displays, intent, display.getDisplayId());
        }
    }

    /**
     * The OEM framework serves IWindowManager.registerIOnTouchListener with a pointer event
     * listener of the window manager on the given display, which calls onTouchEvent with
     * MotionEvent.getAction() of every touch there; the app restarts its lock countdown on
     * ACTION_UP and stops it on any other action. The API30 base has no such call, so a gesture
     * input monitor of the rear display passes the same actions, and the OEM display 3 is dropped.
     */
    public static void registerTouchListener(IOnTouchListener listener, int display) {
        Context context = ActivityThread.currentApplication();
        DisplayManager displays = context.getSystemService(DisplayManager.class);
        TouchMonitor monitor =
                new TouchMonitor(context.getSystemService(InputManager.class), displays, listener);

        displays.registerDisplayListener(monitor, new Handler(Looper.getMainLooper()));

        for (Display rear : displays.getDisplays()) {
            monitor.onDisplayAdded(rear.getDisplayId());
        }
    }

    static void start(Context context, DisplayManager displays, Intent intent, int id) {
        if (!rear(displays, id)) {
            return;
        }

        Log.i(TAG, "starting the rear screen home on display " + id);
        context.startActivity(
                intent, ActivityOptions.makeBasic().setLaunchDisplayId(id).toBundle());
    }

    static boolean rear(DisplayManager displays, int id) {
        Display display = displays.getDisplay(id);

        return display != null && UNIQUE_ID.equals(uniqueId(display));
    }

    // Display.getUniqueId is hidden; the app runs in the system UID that may read it.
    static String uniqueId(Display display) {
        try {
            return (String) Display.class.getMethod("getUniqueId").invoke(display);
        } catch (ReflectiveOperationException e) {
            Log.w(TAG, "display unique ID unavailable: " + e);

            return null;
        }
    }

    /** Follows the rear display: one gesture monitor on its current number. */
    static final class TouchMonitor implements DisplayManager.DisplayListener {
        final InputManager input;
        final DisplayManager displays;
        final IOnTouchListener listener;
        int display = Display.INVALID_DISPLAY;
        InputMonitor monitor;
        Receiver receiver;

        TouchMonitor(InputManager input, DisplayManager displays, IOnTouchListener listener) {
            this.input = input;
            this.displays = displays;
            this.listener = listener;
        }

        @Override
        public void onDisplayAdded(int id) {
            if (id == display || !rear(displays, id)) {
                return;
            }

            close();

            // InputManager.monitorGestureInput is hidden; the platform-signed app may call it.
            try {
                monitor =
                        (InputMonitor)
                                InputManager.class
                                        .getMethod("monitorGestureInput", String.class, int.class)
                                        .invoke(input, TAG, id);
            } catch (ReflectiveOperationException e) {
                Log.w(TAG, "rear screen touches unavailable on display " + id + ": " + e);

                return;
            }

            receiver = new Receiver(monitor.getInputChannel(), listener);
            display = id;
            Log.i(TAG, "passing the rear screen touches of display " + id);
        }

        @Override
        public void onDisplayChanged(int id) {}

        @Override
        public void onDisplayRemoved(int id) {
            if (id == display) {
                close();
            }
        }

        void close() {
            if (receiver != null) {
                receiver.dispose();
                monitor.dispose();
                receiver = null;
                monitor = null;
                display = Display.INVALID_DISPLAY;
            }
        }
    }

    static final class Receiver extends InputEventReceiver {
        final IOnTouchListener listener;

        Receiver(InputChannel channel, IOnTouchListener listener) {
            super(channel, Looper.getMainLooper());
            this.listener = listener;
        }

        @Override
        public void onInputEvent(InputEvent event) {
            try {
                if (event instanceof MotionEvent) {
                    listener.onTouchEvent(((MotionEvent) event).getAction());
                }
            } catch (RemoteException e) {
                Log.w(TAG, "rear screen touch listener failed: " + e);
            } finally {
                finishInputEvent(event, false);
            }
        }
    }
}
