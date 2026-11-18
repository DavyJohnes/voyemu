package com.voyemu.input;

import java.util.Arrays;

/** Input authentication and hold transitions without Android transport fixtures. */
public final class WheelInputSessionTest {
    private static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    private static void refused(Runnable action) {
        try {
            action.run();
        } catch (IllegalArgumentException expected) {
            return;
        }

        throw new AssertionError("Invalid input accepted");
    }

    public static void main(String[] args) {
        long[] clock = {100};
        String token = "0123456789abcdef0123456789abcdef";
        WheelInputSession session =
                new WheelInputSession("input_test_session", token, 0, () -> clock[0]);

        check(session.authenticates(1, "input_test_session", token));
        check(!session.authenticates(1, "input_test_session", "00000000000000000000000000000000"));
        check(WheelInputSession.controlCallerAllowed(2000, true));
        check(!WheelInputSession.controlCallerAllowed(1000, true));
        check(!WheelInputSession.controlCallerAllowed(0, false));
        WheelInputSession.Command down = session.prepare(1, "volume_down", "down", "pointer");
        check(down.key == 1 && Arrays.equals(down.actions, new int[] {WheelInputSession.DOWN}));
        check(session.held().isEmpty());
        session.commit(down);
        refused(() -> session.prepare(1, "volume_down", "up", "pointer"));
        refused(() -> session.prepare(2, "volume_down", "up", "other"));
        clock[0] += 999;
        check(!session.startLong("volume_down", "pointer"));
        clock[0]++;
        check(session.startLong("volume_down", "pointer"));
        check(!session.startLong("volume_down", "pointer"));
        WheelInputSession.Command up = session.prepare(2, "volume_down", "up", "pointer");
        check(
                Arrays.equals(
                        up.actions, new int[] {WheelInputSession.LONG_STOP, WheelInputSession.UP}));
        session.commit(up);
        check(session.held().isEmpty() && session.sequence() == 2);
        session.commit(session.prepare(3, "photo", "down", "keyboard"));
        clock[0] += 2000;
        WheelInputSession.Command cancel = session.prepare(4, "photo", "cancel", "keyboard");
        check(Arrays.equals(cancel.actions, new int[] {WheelInputSession.UP}));
        session.commit(cancel);
        check(session.held().isEmpty());
        System.out.println("Wheel input authentication, ownership, hold and cancellation passed");
    }
}
