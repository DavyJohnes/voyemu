package com.voyemu.prepare;

import com.voyemu.input.PassionWheelSession;

import java.util.ArrayList;
import java.util.Map;

public final class PassionWheelSessionTest {
    interface Action {
        void run() throws Exception;
    }

    static void check(boolean ok) {
        if (!ok) {
            throw new AssertionError();
        }
    }

    static void reject(Action action) throws Exception {
        boolean rejected = false;

        try {
            action.run();
        } catch (IllegalArgumentException expected) {
            rejected = true;
        }

        check(rejected);
    }

    public static void main(String[] args) throws Exception {
        var s =
                new PassionWheelSession(
                        "session-123", "0123456789abcdef0123456789abcdef", 0, Map.of(), false);

        check(s.authenticates(1, "session-123", "0123456789abcdef0123456789abcdef"));
        check(!s.authenticates(1, "session-123", "1123456789abcdef0123456789abcdef"));
        check(
                PassionWheelSession.key("ok") == 85
                        && PassionWheelSession.key("next") == 87
                        && PassionWheelSession.key("previous") == 88
                        && PassionWheelSession.key("volume_up") == 24
                        && PassionWheelSession.key("volume_down") == 25);
        check(PassionWheelSession.key("acc") == 0 && PassionWheelSession.key("phone") == 0);
        var saved = new ArrayList<Map<String, String>>();
        var down = s.accept(1, "next", "down", "owner", (n, h) -> saved.add(h));
        check(
                down.key == 87
                        && down.action == 0
                        && s.pendingSubmission()
                        && saved.get(0).containsKey("next"));
        reject(() -> s.accept(2, "next", "up", "owner", (n, h) -> {}));
        s.confirmed(down, (n, h) -> saved.add(h));
        reject(() -> s.accept(2, "next", "up", "other", (n, h) -> {}));
        var up = s.accept(2, "next", "up", "owner", (n, h) -> saved.add(h));
        check(up.action == 1 && saved.get(saved.size() - 1).containsKey("next"));
        s.confirmed(up, (n, h) -> saved.add(h));
        check(!s.hasHolds() && !saved.get(saved.size() - 1).containsKey("next"));
        reject(() -> s.accept(2, "ok", "down", "owner", (n, h) -> {}));
        reject(() -> s.accept(3, "acc", "down", "owner", (n, h) -> {}));
        var pending =
                new PassionWheelSession(
                        "session-123", "0123456789abcdef0123456789abcdef", 3, Map.of(), true);
        reject(() -> pending.accept(4, "ok", "down", "owner", (n, h) -> {}));
        System.out.println(
                "Passion Android media key mapping, ownership and uncertain submission PASS");
    }
}
