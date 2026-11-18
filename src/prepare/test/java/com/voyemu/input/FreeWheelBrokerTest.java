package com.voyemu.input;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Own broker/phase behavior; this is not an Android Binder or visible OEM runtime proof. */
public final class FreeWheelBrokerTest {
    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private interface Checked {
        void run() throws Exception;
    }

    private static void rejected(Checked action) throws Exception {
        try {
            action.run();
        } catch (IllegalArgumentException expected) {
            return;
        }

        throw new AssertionError("invalid input accepted");
    }

    private static void broker() {
        checkCallerAndWire();
        checkRegistrationPolicy();
        checkFailureAndCapacity();
        checkGenerationReplacement();
    }

    private static void checkCallerAndWire() {
        check(
                FreeWheelBroker.controlCallerAllowed(0, true)
                        && FreeWheelBroker.controlCallerAllowed(2000, true),
                "root/shell DUMP");
        check(
                !FreeWheelBroker.controlCallerAllowed(1000, true)
                        && !FreeWheelBroker.controlCallerAllowed(2000, false)
                        && !FreeWheelBroker.controlCallerAllowed(10001, true),
                "system/app/no-permission control rejected");
        List<Object> wire = new ArrayList<>();
        FreeWheelBroker.writeCallback(
                new FreeWheelBroker.Wire() {
                    @Override
                    public void token(String value) {
                        wire.add("token:" + value);
                    }

                    @Override
                    public void number(int value) {
                        wire.add(value);
                    }

                    @Override
                    public void text(String value) {
                        wire.add(value);
                    }

                    @Override
                    public void nullBundle() {
                        wire.add("null-bundle");
                    }
                },
                173,
                8192,
                "com.qinggan.app.launcher",
                0);
        check(
                wire.equals(
                        Arrays.asList(
                                "token:com.qinggan.system.IKeyManagerCallback",
                                1,
                                173,
                                8192,
                                0,
                                "null-bundle",
                                -1,
                                "com.qinggan.app.launcher",
                                0)),
                "exact callback parcel body/order");
    }

    private static void checkRegistrationPolicy() {
        FreeWheelBroker broker = new FreeWheelBroker();
        Object camera = new Object();
        List<String> observed = new ArrayList<>();

        check(
                FreeWheelBroker.registrationAllowed(
                        "com.qinggan.app.launcher_keyeventtool",
                        new String[] {"com.qinggan.app.launcher"}),
                "actual registration suffix");
        check(
                !FreeWheelBroker.registrationAllowed(
                        "com.qinggan.app.launcher.foreign",
                        new String[] {"com.qinggan.app.launcher"}),
                "package prefix rejected");
        check(
                !FreeWheelBroker.registrationAllowed(
                        "com.qinggan.app.launcher", new String[] {"foreign"}),
                "foreign owner rejected");
        check(
                broker.register(
                                1000,
                                "com.qinggan.app.launcher",
                                0,
                                0,
                                1,
                                new int[] {173},
                                camera,
                                (key, action, registration, type) -> {
                                    observed.add(
                                            key + ":" + action + ":" + registration + ":" + type);

                                    return false;
                                })
                        == 0,
                "registered");
        FreeWheelBroker.Result delivered = broker.dispatch(173, FreeWheelBroker.SHORT);
        check(
                delivered.delivered == 1
                        && delivered.handled == 0
                        && delivered.status().equals("callbacks"),
                "OEM false is still delivered");
        check(
                observed.equals(Arrays.asList("173:8192:com.qinggan.app.launcher:0")),
                "callback2 targeting");
        check(
                broker.dispatch(130, FreeWheelBroker.SHORT).status().equals("no-consumer"),
                "key filter");
        check(!broker.unregister(2000, "com.qinggan.app.launcher", 0, camera), "foreign UID");
        check(
                !broker.unregister(1000, "com.qinggan.app.launcher", 0, new Object()),
                "foreign callback");
        check(
                broker.register(
                                2000,
                                "com.qinggan.app.launcher",
                                0,
                                0,
                                1,
                                new int[0],
                                new Object(),
                                (a, b, c, d) -> true)
                        == -1,
                "foreign replacement");
        check(
                broker.register(
                                1000,
                                "intercept",
                                1,
                                50,
                                1,
                                new int[0],
                                new Object(),
                                (a, b, c, d) -> true)
                        == 0,
                "intercept");
        check(
                broker.dispatch(173, FreeWheelBroker.SHORT).delivered == 1 && observed.size() == 1,
                "handled intercept consumes under own policy");
        check(
                broker.register(
                                1000,
                                "bad",
                                2,
                                0,
                                1,
                                new int[0],
                                new Object(),
                                (a, b, c, d) -> true)
                        == -1,
                "unknown listener type");
        check(
                broker.register(
                                1000,
                                "bad",
                                0,
                                0,
                                2,
                                new int[0],
                                new Object(),
                                (a, b, c, d) -> true)
                        == -1,
                "unknown flags");
    }

    private static void checkFailureAndCapacity() {
        FreeWheelBroker broken = new FreeWheelBroker();

        broken.register(
                1000,
                "broken",
                0,
                0,
                1,
                new int[0],
                new Object(),
                (a, b, c, d) -> {
                    throw new Exception("transport");
                });
        FreeWheelBroker.Result failed = broken.dispatch(173, FreeWheelBroker.DOWN);
        check(
                failed.failed == 1 && failed.delivered == 0 && broken.size() == 0,
                "failed callback never acknowledged");

        for (int i = 0; i < 32; i++) {
            check(
                    broken.register(
                                    1000,
                                    "p" + i,
                                    0,
                                    0,
                                    1,
                                    new int[0],
                                    new Object(),
                                    (a, b, c, d) -> false)
                            == 0,
                    "capacity");
        }

        check(
                broken.register(
                                1000,
                                "overflow",
                                0,
                                0,
                                1,
                                new int[0],
                                new Object(),
                                (a, b, c, d) -> false)
                        == -1,
                "bounded registration");
    }

    private static void checkGenerationReplacement() {
        // A failed in-flight older callback must not remove its replacement, even with the same
        // Binder.
        FreeWheelBroker generation = new FreeWheelBroker();
        Object shared = new Object();

        generation.register(
                1000,
                "same",
                0,
                0,
                1,
                new int[0],
                shared,
                (a, b, c, d) -> {
                    generation.register(
                            1000, "same", 0, 0, 1, new int[0], shared, (w, x, y, z) -> true);
                    throw new Exception("old generation");
                });
        generation.dispatch(173, FreeWheelBroker.DOWN);
        check(
                generation.size() == 1 && generation.dispatch(173, FreeWheelBroker.UP).handled == 1,
                "replacement survives stale failure");
    }

    private static void phases() throws Exception {
        String token = "0123456789abcdef0123456789abcdef";
        FreeWheelPhases state = new FreeWheelPhases("free_wheel_test", token, 0);

        check(state.authenticates(1, "free_wheel_test", token), "authentication");
        check(
                !state.authenticates(2, "free_wheel_test", token)
                        && !state.authenticates(1, "foreign_session", token),
                "schema/session");
        check(
                !state.authenticates(1, "free_wheel_test", "00000000000000000000000000000000"),
                "token");
        long[] saved = {0};
        FreeWheelPhases.Watermark storage = sequence -> saved[0] = sequence;
        check(
                Arrays.equals(
                                state.accept(1, "photo", "down", "pointer", 100, storage),
                                new int[] {4096})
                        && saved[0] == 1,
                "down committed");
        rejected(() -> state.accept(1, "photo", "up", "pointer", 200, storage));
        rejected(() -> state.accept(2, "photo", "up", "foreign", 200, storage));
        rejected(() -> state.accept(2, "photo", "down", "pointer", 200, storage));
        check(
                Arrays.equals(
                        state.accept(2, "photo", "up", "pointer", 1099, storage),
                        new int[] {8192, 65536}),
                "short then up");
        state.accept(3, "photo", "down", "pointer", 2000, storage);
        check(
                Arrays.equals(
                        state.accept(4, "photo", "up", "pointer", 3000, storage),
                        new int[] {32768, 524288, 65536}),
                "explicit photo long threshold");
        state.accept(5, "volume_down", "down", "pointer", 4000, storage);
        check(
                Arrays.equals(
                        state.accept(6, "volume_down", "up", "pointer", 9000, storage),
                        new int[] {8192, 65536}),
                "no invented long mute");
        state.accept(7, "photo", "down", "pointer", 10000, storage);
        check(
                Arrays.equals(
                        state.accept(8, "photo", "cancel", "pointer", 20000, storage),
                        new int[] {65536}),
                "cancel never clicks");

        try {
            state.accept(
                    9,
                    "photo",
                    "down",
                    "pointer",
                    21000,
                    sequence -> {
                        throw new java.io.IOException("disk");
                    });
            throw new AssertionError("storage failure lost");
        } catch (java.io.IOException expected) {
            check(
                    state.sequence() == 8 && state.heldCount() == 0,
                    "no phantom hold on storage failure");
        }

        FreeWheelPhases restored = new FreeWheelPhases("free_wheel_test", token, 9);
        Map<String, String> holds = new LinkedHashMap<>();
        holds.put("photo", "pointer");
        restored.restore(holds);
        check(
                Arrays.equals(
                        restored.accept(10, "photo", "up", "pointer", 1, storage),
                        new int[] {65536}),
                "restart releases without synthetic click");
        rejected(() -> state.accept(9, "unknown", "down", "pointer", 21000, storage));
        rejected(() -> state.accept(9, "photo", "hold", "pointer", 21000, storage));
        check(
                FreeWheelPhases.key("custom") == 3090 && FreeWheelPhases.key("photo") == 173,
                "no A9 custom65 remapping");
    }

    private static void protocol() throws Exception {
        Object token = new Object(), callback = new Object();
        List<Object> seen = new ArrayList<>();
        FreeWheelProtocol.Operations operations =
                new FreeWheelProtocol.Operations() {
                    @Override
                    public int register(
                            Object t,
                            Object c,
                            int[] keys,
                            String name,
                            int flags,
                            int priority,
                            int type) {
                        seen.addAll(Arrays.asList(t, c, keys.length, name, flags, priority, type));

                        return 0;
                    }

                    @Override
                    public int unregister(Object c, String name, int type) {
                        seen.addAll(Arrays.asList(c, name, type));

                        return 0;
                    }
                };

        class Packet implements FreeWheelProtocol.Input {
            final List<Object> values;
            int offset;
            final String descriptor;

            Packet(String descriptor, Object... values) {
                this.descriptor = descriptor;
                this.values = Arrays.asList(values);
            }

            @Override
            public void enforce(String value) {
                if (!descriptor.equals(value)) {
                    throw new SecurityException("token");
                }
            }

            @Override
            public Object binder() {
                return values.get(offset++);
            }

            @Override
            public int[] keys() {
                return (int[]) values.get(offset++);
            }

            @Override
            public String text() {
                return (String) values.get(offset++);
            }

            @Override
            public int number() {
                return (Integer) values.get(offset++);
            }

            @Override
            public int remaining() {
                return values.size() - offset;
            }
        }

        for (int code : new int[] {1, 3}) {
            seen.clear();
            check(
                    FreeWheelProtocol.transact(
                                    code,
                                    0,
                                    new Packet(
                                            FreeWheelBroker.SERVICE,
                                            token,
                                            callback,
                                            new int[] {173},
                                            "app",
                                            1,
                                            50),
                                    operations)
                            == 0,
                    "registration reply");
            check(
                    seen.equals(Arrays.asList(token, callback, 1, "app", 1, 50, code == 1 ? 0 : 1)),
                    "actual registration parcel order/type");
        }

        for (int code : new int[] {2, 4}) {
            seen.clear();
            FreeWheelProtocol.transact(
                    code, 0, new Packet(FreeWheelBroker.SERVICE, callback, "app"), operations);
            check(
                    seen.equals(Arrays.asList(callback, "app", code == 2 ? 0 : 1)),
                    "unregister parcel");
        }

        seen.clear();
        rejected(() ->
                FreeWheelProtocol.transact(1, 1, new Packet(FreeWheelBroker.SERVICE), operations));
        rejected(() ->
                FreeWheelProtocol.transact(
                        2,
                        0,
                        new Packet(FreeWheelBroker.SERVICE, callback, "app", "trailing"),
                        operations));

        try {
            FreeWheelProtocol.transact(2, 0, new Packet("foreign", callback, "app"), operations);
            throw new AssertionError("foreign token");
        } catch (SecurityException expected) {
        }

        for (int code : new int[] {0, 5, 6, 9, 16, 17}) {
            try {
                FreeWheelProtocol.transact(
                        code, 0, new Packet(FreeWheelBroker.SERVICE), operations);
                throw new AssertionError("unsupported ACK");
            } catch (UnsupportedOperationException expected) {
            }
        }

        check(seen.isEmpty(), "invalid packets produce no mutation");
    }

    public static void main(String[] args) throws Exception {
        broker();
        phases();
        protocol();
        System.out.println(
                "Free wheel broker/phase tests PASS (host semantics, not Android or visible-effect"
                        + " proof)");
    }
}
