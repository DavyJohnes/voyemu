package com.voyemu.output;

import android.app.ActivityThread;
import android.app.Application;
import android.app.IOnTouchListener;
import android.content.Context;
import android.hardware.input.InputManager;
import android.os.Looper;
import android.os.RemoteException;
import android.view.Display;
import android.view.InputEvent;
import android.view.InputEventReceiver;
import android.view.MotionEvent;

import java.util.ArrayList;
import java.util.List;

public class RearScreenHostTest {
    static final String REAR = "virtual:com.android.emulator.multidisplay:1234563";

    static void check(boolean b, String s) {
        if (!b) {
            throw new AssertionError(s);
        }
    }

    static Display display(int id, String unique) {
        var d = new Display(id, "display " + id);

        d.uniqueId = unique;

        return d;
    }

    static final class Listener implements IOnTouchListener {
        final List<Integer> actions = new ArrayList<>();
        boolean fail;

        @Override
        public void onTouchEvent(int action) throws RemoteException {
            actions.add(action);

            if (fail) {
                throw new RemoteException();
            }
        }
    }

    static InputEventReceiver last() {
        return InputEventReceiver.RECEIVERS.get(InputEventReceiver.RECEIVERS.size() - 1);
    }

    public static void main(String[] a) {
        ActivityThread.application = new Application("com.qinggan.app.thirdscreen");
        var input = new InputManager();
        Context.SERVICES.put(InputManager.class, input);
        var displays = Context.DISPLAYS;
        displays.add(display(0, "local:0"));
        displays.add(display(2, "virtual:com.android.emulator.multidisplay:1234562"));
        var listener = new Listener();

        // The OEM registration names display 3; no display carries the rear unique ID yet.
        RearScreen.registerTouchListener(listener, 3);
        check(input.monitors.isEmpty(), "no monitor before the rear display exists");

        displays.add(display(3, "virtual:com.android.emulator.multidisplay:1234564"));
        check(input.monitors.isEmpty(), "display 3 without the rear unique ID is not monitored");

        displays.add(display(5, REAR));
        check(input.monitors.size() == 1, "the rear display gets one gesture monitor");
        var first = input.monitors.get(0);
        var receiver = last();
        check(
                first.name.equals("VoyemuRearScreen")
                        && receiver.channel == first.getInputChannel()
                        && receiver.channel.display == 5
                        && receiver.looper == Looper.getMainLooper(),
                "the monitor of display 5 delivers on the main looper");

        // ACTION_DOWN, ACTION_MOVE, ACTION_UP and a second pointer pass through unchanged.
        var events = new ArrayList<InputEvent>();

        for (int action : new int[] {0, 2, 1, 0x105}) {
            var event = new MotionEvent(action);
            events.add(event);
            receiver.deliver(event);
        }

        InputEvent other = new InputEvent();
        events.add(other);
        receiver.deliver(other);
        check(
                listener.actions.equals(List.of(0, 2, 1, 0x105)),
                "every touch action reaches the OEM listener in order: " + listener.actions);
        check(receiver.finished.equals(events), "every event is finished unhandled");

        // A failing listener call still finishes the event and keeps the monitor.
        listener.fail = true;
        var failed = new MotionEvent(1);
        receiver.deliver(failed);
        listener.fail = false;
        check(
                receiver.finished.get(receiver.finished.size() - 1) == failed && !receiver.disposed,
                "a RemoteException finishes the event and keeps the monitor");

        displays.changed(5);
        check(input.monitors.size() == 1 && !receiver.disposed, "a display change keeps it");

        // The provider restarts: the rear display goes away and returns under a new number.
        displays.remove(2);
        check(!receiver.disposed, "removing another display keeps the monitor");
        displays.remove(5);
        check(receiver.disposed && first.disposed, "removing the rear display disposes both");
        displays.add(display(7, REAR));
        var second = input.monitors.get(1);
        var again = last();
        check(
                input.monitors.size() == 2
                        && again != receiver
                        && again.channel.display == 7
                        && !again.disposed,
                "the returning rear display 7 gets a new monitor");
        listener.actions.clear();
        again.deliver(new MotionEvent(1));
        check(listener.actions.equals(List.of(1)), "touches of display 7 reach the listener");
        check(!second.disposed, "the current monitor stays open");
        displays.add(display(7, REAR));
        check(
                input.monitors.size() == 2 && !second.disposed,
                "a repeated add of the monitored display opens no second monitor");

        System.out.println(
                "RearScreen touches PASS: rear unique ID only, display 3 ignored, actions in order,"
                        + " every event finished unhandled, listener failure, display change,"
                        + " removal disposes, renumbered rear display monitored again");
    }
}
