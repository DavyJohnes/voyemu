package com.voyemu.input;

import android.content.Context;
import android.media.AudioManager;

public class TerminalTest {
    static void check(boolean x) {
        if (!x) {
            throw new AssertionError();
        }
    }

    public static void main(String[] a) {
        var c = new Context();
        var m = new AudioManager();

        c.service = m;
        PassionWheelTerminal.submit(c, new PassionWheelSession.Event(87, 0, 0, "next"));
        check(m.calls == 1 && m.event.key == 87 && m.event.action == 0);
        PassionWheelTerminal.submit(c, new PassionWheelSession.Event(87, 1, 32, "next"));
        check(m.calls == 2 && m.event.action == 1 && m.event.flags == 32);
        PassionWheelTerminal.submit(c, new PassionWheelSession.Event(24, 0, 0, "volume_up"));
        check(m.calls == 3 && m.stream == 3 && m.direction == 1);
        PassionWheelTerminal.submit(c, new PassionWheelSession.Event(24, 1, 0, "volume_up"));
        check(m.calls == 3);
        PassionWheelTerminal.submit(c, new PassionWheelSession.Event(25, 0, 0, "volume_down"));
        check(m.calls == 4 && m.direction == -1);
        boolean rejected = false;

        try {
            PassionWheelTerminal.submit(c, new PassionWheelSession.Event(999, 0, 0, "bad"));
        } catch (IllegalArgumentException expected) {
            rejected = true;
        }

        check(rejected);
        System.out.println(
                "Android media/volume dispatch and cancellation PASS; consumer effect remains"
                        + " unverified");
    }
}
