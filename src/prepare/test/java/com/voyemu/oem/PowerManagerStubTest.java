package com.voyemu.oem;

import android.content.Context;
import android.graphics.Color;
import android.hardware.display.DisplayManager;
import android.os.Looper;
import android.view.Display;
import android.view.View;
import android.view.WindowManager;

import com.qinggan.power.IPowerManagerListener;

import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

/**
 * The qg.power_manager stub of a Free profile turns each OEM screen off on its own display: screen
 * 1, the driver screen of the curtain tile DRV Off, is the default display and screen 2, the tile
 * Co-DRV Off, is the passenger display. Each screen gets a black window that a click removes, and
 * the listeners receive the status of both screens, 5 for off and 1 for on.
 */
public final class PowerManagerStubTest {
    private static final int DRIVER = 1, PASSENGER = 2;

    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static Map<View, WindowManager.LayoutParams> windows(Context context, int display) {
        WindowManager manager = context.windowManager(display);

        return manager == null ? Map.of() : manager.windows;
    }

    private static View blackout(Context context, int display, String title) {
        Map<View, WindowManager.LayoutParams> windows = windows(context, display);

        check(windows.size() == 1, "one window on display " + display);
        var entry = windows.entrySet().iterator().next();
        WindowManager.LayoutParams params = entry.getValue();
        check(
                params.width == WindowManager.LayoutParams.MATCH_PARENT
                        && params.height == WindowManager.LayoutParams.MATCH_PARENT
                        && params.type == WindowManager.LayoutParams.TYPE_SYSTEM_ERROR
                        && (params.flags & WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE) != 0,
                "full screen system window without focus on display " + display);
        check(title.contentEquals(params.title), "window title " + params.title);
        check(entry.getKey().background == Color.BLACK, "black window");

        return entry.getKey();
    }

    private static void status(
            PowerManagerStub stub, List<String> changes, int driver, int passenger)
            throws Exception {
        check(
                changes.equals(List.of(driver + "," + passenger)),
                "listener status " + changes + " instead of " + driver + "," + passenger);
        changes.clear();
        check(stub.isScreenOffForScreen(DRIVER) == (driver == 5), "driver screen off");
        check(stub.isScreenOffForScreen(PASSENGER) == (passenger == 5), "passenger screen off");
        check(stub.getAVNStatusForScreen(DRIVER) == driver, "driver AVN status");
        check(stub.getAVNStatusForScreen(PASSENGER) == passenger, "passenger AVN status");
    }

    public static void main(String[] args) throws Exception {
        DisplayManager displays = new DisplayManager();

        displays.displays.addAll(
                Arrays.asList(
                        new Display(0, "Built-in Screen", 1920, 720),
                        new Display(2, "Emulator2D", 1920, 720)));
        Context context = new Context(displays);
        PowerManagerStub stub = new PowerManagerStub(context);
        List<String> changes = new ArrayList<>();
        IPowerManagerListener listener =
                (IPowerManagerListener)
                        Proxy.newProxyInstance(
                                IPowerManagerListener.class.getClassLoader(),
                                new Class<?>[] {IPowerManagerListener.class},
                                (proxy, method, values) -> {
                                    if (method.getName().equals("onDoubleScreenStatusChange")) {
                                        changes.add(values[0] + "," + values[1]);
                                    }

                                    return null;
                                });

        check(stub.registerListener("test", listener), "listener registered");
        Looper.drain();
        status(stub, changes, 1, 1);

        // DRV Off: the default display goes black, the passenger display keeps its content.
        stub.enterScreenOffForScreen(DRIVER);
        Looper.drain();
        View driver = blackout(context, 0, "Voyemu driver screen off");
        check(windows(context, 2).isEmpty(), "passenger display untouched");
        status(stub, changes, 5, 1);

        // A repeated command keeps the one window and sends no status.
        stub.enterScreenOffForScreen(DRIVER);
        Looper.drain();
        check(windows(context, 0).size() == 1 && changes.isEmpty(), "repeated driver off");

        // Co-DRV Off: the passenger display goes black as well.
        stub.enterScreenOffForScreen(PASSENGER);
        Looper.drain();
        View passenger = blackout(context, 2, "Voyemu passenger screen off");
        status(stub, changes, 5, 5);

        // A touch on the black driver screen wakes only that screen.
        check(driver.performClick(), "driver blackout takes clicks");
        Looper.drain();
        check(windows(context, 0).isEmpty(), "driver blackout removed");
        check(windows(context, 2).containsKey(passenger), "passenger stays off");
        status(stub, changes, 1, 5);

        // The OEM exit command wakes the passenger screen the same way.
        stub.exitScreenOffForScreen(PASSENGER);
        Looper.drain();
        check(windows(context, 2).isEmpty(), "passenger blackout removed");
        status(stub, changes, 1, 1);

        // Waking a screen that is on changes nothing; the curtain toggles by the reported status.
        stub.exitScreenOffForScreen(DRIVER);
        Looper.drain();
        check(changes.isEmpty() && windows(context, 0).isEmpty(), "driver already on");

        // The driver screen can go off again after a wake by the tile.
        stub.enterScreenOffForScreen(DRIVER);
        Looper.drain();
        blackout(context, 0, "Voyemu driver screen off");
        status(stub, changes, 5, 1);
        stub.exitScreenOffForScreen(DRIVER);
        Looper.drain();
        check(windows(context, 0).isEmpty(), "driver blackout removed by the tile");
        status(stub, changes, 1, 1);

        // Unknown screens are ignored.
        stub.enterScreenOffForScreen(0);
        stub.enterScreenOffForScreen(3);
        Looper.drain();
        check(
                changes.isEmpty() && windows(context, 0).isEmpty() && windows(context, 2).isEmpty(),
                "unknown screens ignored");

        // Without its display a screen stays on and the listeners get no change.
        displays.displays.clear();
        displays.displays.add(new Display(2, "Emulator2D", 1920, 720));
        stub.enterScreenOffForScreen(DRIVER);
        Looper.drain();
        check(
                changes.isEmpty() && !stub.isScreenOffForScreen(DRIVER),
                "missing driver display keeps the screen on");
        System.out.println("driver and passenger screens turn off and wake independently");
    }
}
