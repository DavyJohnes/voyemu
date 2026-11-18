package com.voyemu.input;

import java.util.LinkedHashMap;

public final class FreeKeyInputSessionTest {
    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private interface Action {
        void run() throws Exception;
    }

    private static void denied(Action action) throws Exception {
        try {
            action.run();
        } catch (IllegalArgumentException expected) {
            return;
        }

        throw new AssertionError("invalid input accepted");
    }

    public static void main(String[] args) throws Exception {
        String id = "free_key_session", token = "0123456789abcdef0123456789abcdef";
        var session = new FreeKeyInputSession(id, token, 0, new LinkedHashMap<>());

        check(session.authenticates(1, id, token), "auth");
        check(
                !session.authenticates(1, id, "00000000000000000000000000000000")
                        && !session.authenticates(2, id, token),
                "wrong token/schema");
        check(
                FreeKeyInputSession.callerAllowed(0, true)
                        && FreeKeyInputSession.callerAllowed(2000, true)
                        && !FreeKeyInputSession.callerAllowed(1000, true)
                        && !FreeKeyInputSession.callerAllowed(2000, false),
                "root/shell+DUMP");
        long[] sequence = {0};
        var held = new LinkedHashMap<String, String>();
        FreeKeyInputSession.Watermark storage =
                (value, holds) -> {
                    sequence[0] = value;
                    held.clear();
                    held.putAll(holds);
                };
        var down = session.accept(1, "photo", "down", "pointer", storage);
        check(
                down.key == 173
                        && down.action == 4096
                        && down.flags == 0
                        && sequence[0] == 1
                        && held.size() == 1,
                "actual DOWN only; no synthesized short");
        session.confirmed(down, storage);
        denied(() -> session.accept(1, "photo", "up", "pointer", storage));
        denied(() -> session.accept(2, "photo", "up", "foreign", storage));
        denied(() -> session.accept(2, "photo", "down", "pointer", storage));
        var cancel = session.accept(2, "photo", "cancel", "pointer", storage);
        check(
                cancel.action == 65536
                        && cancel.flags == 32
                        && held.size() == 1
                        && session.pendingSubmission(),
                "cancel is flagged UP for exact OEM guard");
        denied(() -> session.accept(3, "photo", "cancel", "pointer", storage));
        check(session.hasHolds(), "unknown release must block rotation");
        var recoveredPending = new FreeKeyInputSession(id, token, 2, held, true);
        denied(() -> recoveredPending.accept(3, "photo", "cancel", "pointer", storage));
        check(
                recoveredPending.hasHolds() && recoveredPending.pendingSubmission(),
                "restart retains unknown release");
        session.confirmed(cancel, storage);
        check(held.isEmpty() && !session.pendingSubmission(), "only confirmed release clears hold");
        var packet = new java.util.ArrayList<Object>();
        FreeKeyInputSession.writeEvent(
                new FreeKeyInputSession.Wire() {
                    @Override
                    public void token(String value) {
                        packet.add("token:" + value);
                    }

                    @Override
                    public void number(int value) {
                        packet.add(value);
                    }

                    @Override
                    public void nullBundle() {
                        packet.add("null-bundle");
                    }

                    @Override
                    public void text(String value) {
                        packet.add(value);
                    }
                },
                cancel);
        check(
                packet.equals(
                        java.util.Arrays.asList(
                                "token:com.qinggan.system.IKeyManagerService",
                                1,
                                173,
                                65536,
                                32,
                                "null-bundle",
                                -1,
                                "com.voyemu.input.wheel")),
                "exact targeted transaction7 parcel body");
        var volumeDown = session.accept(3, "volume_down", "down", "pointer", storage);
        session.confirmed(volumeDown, storage);
        var up = session.accept(4, "volume_down", "up", "pointer", storage);
        check(up.action == 65536 && up.flags == 0, "OEM owns short/held timing");
        session.confirmed(up, storage);

        try {
            session.accept(
                    5,
                    "photo",
                    "down",
                    "pointer",
                    (a, b) -> {
                        throw new java.io.IOException("disk");
                    });
            throw new AssertionError("lost storage failure");
        } catch (java.io.IOException expected) {
            check(session.sequence() == 4 && !session.hasHolds(), "no phantom hold");
        }

        var restoredHolds = new LinkedHashMap<String, String>();
        restoredHolds.put("photo", "pointer");
        var restored = new FreeKeyInputSession(id, token, 5, restoredHolds);
        var restoredCancel = restored.accept(6, "photo", "cancel", "pointer", storage);
        check(
                restoredCancel.flags == 32 && restored.hasHolds(),
                "restart retains hold until confirmation");

        try {
            restored.confirmed(
                    restoredCancel,
                    (a, b) -> {
                        throw new java.io.IOException("commit");
                    });
            throw new AssertionError("lost confirm storage failure");
        } catch (java.io.IOException expected) {
            check(
                    restored.pendingSubmission() && restored.hasHolds(),
                    "failed confirmation remains pending");
        }

        denied(() -> restored.accept(7, "photo", "up", "pointer", storage));
        var unknownDown = new FreeKeyInputSession(id, token, 0, new LinkedHashMap<>());
        unknownDown.accept(1, "photo", "down", "pointer", storage);
        denied(() -> unknownDown.accept(2, "photo", "cancel", "pointer", storage));
        check(unknownDown.hasHolds(), "unknown DOWN blocks rotation/replay");
        denied(() -> session.accept(5, "unknown", "down", "pointer", storage));
        denied(() -> session.accept(5, "photo", "hold", "pointer", storage));
        check(FreeKeyInputSession.key("custom") == 3090, "no A9 custom65 remap");
        System.out.println(
                "Free genuine-OEM input session/auth/ownership/durable phases PASS; Binder queue"
                        + " completion is not visible effect");
    }
}
