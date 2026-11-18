package com.voyemu.oem;

import android.content.Context;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.hardware.display.DisplayManager;
import android.os.Handler;
import android.os.Looper;
import android.os.RemoteCallbackList;
import android.os.RemoteException;
import android.view.Display;
import android.view.View;
import android.view.WindowManager;

import com.qinggan.power.IPowerManagerListener;

/** OEM screen commands blank the addressed virtual screen; a click wakes only it. */
public class PowerManagerStub extends com.qinggan.system.IPowerManagerService.Stub {
    private static final String TAG = "VoyemuOemStub";
    private final Context context;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final RemoteCallbackList<IPowerManagerListener> listeners = new RemoteCallbackList<>();
    private final ScreenPowerState state = new ScreenPowerState();
    private final View[] blackouts = new View[2];
    private final WindowManager[] windows = new WindowManager[2];

    public PowerManagerStub(Context context) {
        this.context = context;
    }

    private void notifyScreens() {
        int[] status = state.status();
        int count = listeners.beginBroadcast();

        try {
            for (int i = 0; i < count; i++) {
                try {
                    listeners.getBroadcastItem(i).onDoubleScreenStatusChange(status[0], status[1]);
                } catch (RemoteException failure) {
                    android.util.Log.w(TAG, "screen listener disconnected", failure);
                }
            }
        } finally {
            listeners.finishBroadcast();
        }
    }

    private void changeScreen(final int screen, final boolean off) {
        if (!ScreenPowerState.supports(screen)) {
            return;
        }

        main.post(
                new Runnable() {
                    @Override
                    public void run() {
                        if (state.isOff(screen) == off) {
                            return;
                        }

                        int index = screen - 1;

                        if (off) {
                            DisplayManager displays =
                                    context.getSystemService(DisplayManager.class);
                            Display[] available = displays.getDisplays();
                            int[] ids = new int[available.length],
                                    widths = new int[available.length],
                                    heights = new int[available.length];
                            String[] names = new String[available.length];

                            for (int i = 0; i < available.length; i++) {
                                ids[i] = available[i].getDisplayId();
                                names[i] = available[i].getName();
                                widths[i] = available[i].getWidth();
                                heights[i] = available[i].getHeight();
                            }

                            int selected =
                                    ScreenDisplayTarget.select(screen, ids, names, widths, heights);

                            if (selected < 0) {
                                return;
                            }

                            Display target = available[selected];
                            Context displayContext = context.createDisplayContext(target);
                            WindowManager manager =
                                    displayContext.getSystemService(WindowManager.class);
                            View view = new View(displayContext);
                            view.setBackgroundColor(Color.BLACK);
                            view.setOnClickListener(
                                    new View.OnClickListener() {
                                        @Override
                                        public void onClick(View clicked) {
                                            changeScreen(screen, false);
                                        }
                                    });
                            WindowManager.LayoutParams params =
                                    new WindowManager.LayoutParams(
                                            WindowManager.LayoutParams.MATCH_PARENT,
                                            WindowManager.LayoutParams.MATCH_PARENT,
                                            WindowManager.LayoutParams.TYPE_SYSTEM_ERROR,
                                            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                                                    | WindowManager.LayoutParams
                                                            .FLAG_LAYOUT_IN_SCREEN
                                                    | WindowManager.LayoutParams
                                                            .FLAG_LAYOUT_NO_LIMITS,
                                            PixelFormat.OPAQUE);
                            params.setTitle(
                                    screen == 1
                                            ? "Voyemu driver screen off"
                                            : "Voyemu passenger screen off");
                            manager.addView(view, params);
                            windows[index] = manager;
                            blackouts[index] = view;
                        } else {
                            windows[index].removeView(blackouts[index]);
                            blackouts[index] = null;
                            windows[index] = null;
                        }

                        state.setOff(screen, off);
                        notifyScreens();
                    }
                });
    }

    @Override
    public boolean registerListener(String name, IPowerManagerListener listener) {
        boolean registered = listeners.register(listener);

        if (registered) {
            main.post(
                    new Runnable() {
                        @Override
                        public void run() {
                            notifyScreens();
                        }
                    });
        }

        return registered;
    }

    @Override
    public boolean unregisterListener(String name, IPowerManagerListener listener) {
        return listeners.unregister(listener);
    }

    @Override
    public void enterScreenOff() {
        logIgnored("enterScreenOff");
    }

    @Override
    public void exitScreenOff() {
        logIgnored("exitScreenOff");
    }

    @Override
    public void enterScreenOffForScreen(int screen) {
        if (ScreenPowerState.supports(screen)) {
            changeScreen(screen, true);
        } else {
            logIgnored("enterScreenOffForScreen");
        }
    }

    @Override
    public void exitScreenOffForScreen(int screen) {
        if (ScreenPowerState.supports(screen)) {
            changeScreen(screen, false);
        } else {
            logIgnored("exitScreenOffForScreen");
        }
    }

    @Override
    public void enterStandby() {
        logIgnored("enterStandby");
    }

    @Override
    public void switchToRunMode() {
        logIgnored("switchToRunMode");
    }

    @Override
    public boolean requestDisplayInStandby(int display) {
        return false;
    }

    @Override
    public boolean abandonDisplayInStandby(int display) {
        return false;
    }

    @Override
    public boolean requestDisplayInStandbyForScreen(int display, int screen) {
        return false;
    }

    @Override
    public boolean abandonDisplayInStandbyForScreen(int display, int screen) {
        return false;
    }

    @Override
    public int getAVNStatus() {
        // AVN (this head unit) is up.
        return 1;
    }

    @Override
    public int getAVNStatusForScreen(int screen) {
        return state.isOff(screen) ? 5 : 1;
    }

    @Override
    public int getCarKeyStatus() {
        return 0;
    }

    @Override
    public int getCurrentMBHighTempLevel() {
        // Normal temperatures.
        return 0;
    }

    @Override
    public int getLCDTemp() {
        return 25;
    }

    @Override
    public boolean isScreenOff() {
        return false;
    }

    @Override
    public boolean isScreenOffForScreen(int screen) {
        return state.isOff(screen);
    }

    private static void logIgnored(String what) {
        android.util.Log.i(TAG, "qg.power_manager: " + what + " ignored (emulator)");
    }
}
