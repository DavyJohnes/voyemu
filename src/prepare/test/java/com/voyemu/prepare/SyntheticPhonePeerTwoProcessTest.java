package com.voyemu.prepare;

import java.lang.reflect.InvocationTargetException;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Path;

/** Real isolated OEM loaders sharing only an Android Binder/ContentResolver test boundary. */
public final class SyntheticPhonePeerTwoProcessTest {
    private static final String NUMBER = "+70000000001",
            TOKEN = "com.voyemu.phone.peer.callback.v1";

    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 4) {
            throw new IllegalArgumentException(
                    "compiled classes, boundary classes, SDK ABI jar, scenario");
        }

        URL classes = Path.of(args[0]).toUri().toURL(), sdk = Path.of(args[2]).toUri().toURL();
        String scenario = args[3];

        try (URLClassLoader boundary =
                        new URLClassLoader(
                                new URL[] {Path.of(args[1]).toUri().toURL(), sdk},
                                ClassLoader.getPlatformClassLoader());
                URLClassLoader phone = new URLClassLoader(new URL[] {classes}, boundary);
                URLClassLoader launcher = new URLClassLoader(new URL[] {classes}, boundary)) {
            Class<?> pp = phone.loadClass("fixture.PhonePeerProcessProbe"),
                    lp = launcher.loadClass("fixture.PhonePeerProcessProbe");
            Class<?> runtime = boundary.loadClass("android.os.PeerTestRuntime");
            check(pp != lp, "probe isolation");
            Object authority = pp.getMethod("create", String.class).invoke(null, "phone"),
                    client = null;

            if ("sameprocess".equals(scenario)) {
                invoke(pp, authority, "sameProcessControl");
                assertLogs(boundary, 96);
                System.out.println(
                        "SyntheticPhonePeerTwoProcessTest PASS sameprocess retained singleton/HFP,"
                                + " exact callbacks, fresh maps/OEM constructor fields");

                return;
            }

            if ("diagnostics".equals(scenario)) {
                runDiagnostics(boundary, runtime, pp, authority);

                return;
            }

            if (!"early".equals(scenario) && !"immediate".equals(scenario)) {
                client = lp.getMethod("create", String.class).invoke(null, "launcher");
            }

            invoke(pp, authority, "control", "connect", "{}");
            invoke(pp, authority, "control", "incoming", "{\"number\":\"" + NUMBER + "\"}");
            check(
                    ("130:" + NUMBER).equals(invoke(pp, authority, "lastCall")),
                    "Phone incoming prerequisite");

            if ("immediate".equals(scenario)) {
                runImmediate(boundary, runtime, lp);

                return;
            }

            if ("early".equals(scenario)) {
                runEarly(boundary, runtime, lp);

                return;
            }

            expect(
                    lp,
                    client,
                    "connected",
                    true,
                    "Launcher remains disconnected from authoritative Phone");
            expect(lp, client, "lastCall", "130:" + NUMBER, "Launcher incoming preserves +7");
            String incoming =
                    wire(boundary, (String) invoke(pp, authority, "control", "snapshot", "{}"));
            String active =
                    wire(boundary, (String) invoke(pp, authority, "control", "answer", "{}"));
            expect(lp, client, "lastCall", "128:" + NUMBER, "Launcher active callback");
            assertWire(boundary, (String) runtime.getField("lastSnapshot").get(null));
            checkDiagnostics(boundary, pp, authority, lp, client);
            checkLateAndDuplicate(runtime, lp, client, active, incoming);
            rejectMaliciousBinder(boundary, runtime, active);

            if ("death".equals(scenario)) {
                runDeath(boundary, runtime, lp, client, classes, active);
            } else if (scenario.startsWith("malformed:")) {
                runMalformed(boundary, runtime, lp, client, active, scenario);
            } else {
                runPersistence(boundary, pp, authority, lp, client, classes);
            }

            assertLogs(boundary, 288);
        }

        System.out.println("SyntheticPhonePeerTwoProcessTest PASS " + scenario);
    }

    private static void runDiagnostics(
            URLClassLoader boundary, Class<?> runtime, Class<?> pp, Object authority)
            throws Exception {
        invoke(
                pp,
                authority,
                "control",
                "configure",
                "{\"peer_name\":\"owner-token-sensitive\",\"contacts\":[{\"name\":\"Иван"
                        + " Иванов\",\"number\":\"+70000000001\"},{\"name\":\"Анна"
                        + " Петрова\",\"number\":\"+70000000002\"}]}");

        for (int i = 0; i < 200; i++) {
            invoke(pp, authority, "lateListener");
        }

        Object root = json(boundary, (String) invoke(pp, authority, "control", "snapshot", "{}"));
        Object diagnostic = get(root, "diagnostics"), events = get(diagnostic, "events");
        check(
                ((Number) get(events, "HFP_REGISTER_OTHER")).intValue() == 201,
                "diagnostic registration counter stops with log cap");
        check(
                ((Number) get(diagnostic, "hfp_listeners")).intValue() == 201,
                "diagnostic local listener count");
        check(
                Integer.valueOf(96)
                        .equals(
                                boundary.loadClass("android.util.Log")
                                        .getMethod("count")
                                        .invoke(null)),
                "production diagnostic log cap96");
        java.lang.reflect.Field subscription =
                runtime.getField("phone").get(null).getClass().getDeclaredField("subscription");
        subscription.setAccessible(true);
        Object peerSubscription = subscription.get(runtime.getField("phone").get(null));
        java.lang.reflect.Field logged = peerSubscription.getClass().getDeclaredField("logged");
        logged.setAccessible(true);
        ((java.util.concurrent.atomic.AtomicInteger) logged.get(peerSubscription))
                .set(Integer.MAX_VALUE);
        invoke(pp, authority, "lateListener");
        invoke(pp, authority, "lateListener");
        check(
                Integer.valueOf(96)
                        .equals(
                                boundary.loadClass("android.util.Log")
                                        .getMethod("count")
                                        .invoke(null)),
                "diagnostic log integer wrap reopens cap");
        boundary.loadClass("android.util.Log").getMethod("assertSafe", int.class).invoke(null, 96);
        System.out.println(
                "SyntheticPhonePeerTwoProcessTest PASS diagnostics"
                        + " counters201/logs96/sensitive-payload-excluded");
    }

    private static void runImmediate(URLClassLoader boundary, Class<?> runtime, Class<?> lp)
            throws Exception {
        boundary.loadClass("android.os.Handler").getField("immediate").setBoolean(null, true);
        Object client = lp.getMethod("create", String.class).invoke(null, "launcher");
        expect(
                lp,
                client,
                "lastCall",
                "130:" + NUMBER,
                "independent main executes initial activation before Binder worker returns"
                        + " from post");
        assertWire(boundary, (String) runtime.getField("lastSnapshot").get(null));
        assertLogs(boundary, 192);
        System.out.println("SyntheticPhonePeerTwoProcessTest PASS immediate");
    }

    private static void runEarly(URLClassLoader boundary, Class<?> runtime, Class<?> lp)
            throws Exception {
        runtime.getField("earlyMutation").set(null, "answer");
        Object client = lp.getMethod("create", String.class).invoke(null, "launcher");
        expect(
                lp,
                client,
                "lastCall",
                "128:" + NUMBER,
                "snapshot N+1 received before authenticated response N must survive without"
                        + " another publish");
        assertWire(boundary, (String) runtime.getField("lastSnapshot").get(null));
        assertLogs(boundary, 192);
        System.out.println("SyntheticPhonePeerTwoProcessTest PASS early");
    }

    private static void checkDiagnostics(
            URLClassLoader boundary, Class<?> pp, Object authority, Class<?> lp, Object client)
            throws Exception {
        Object phoneDiagnostic =
                get(
                        json(boundary, (String) invoke(pp, authority, "control", "snapshot", "{}")),
                        "diagnostics");
        Object mirrorDiagnostic =
                get(json(boundary, (String) invoke(lp, client, "localSnapshot")), "diagnostics");

        check(
                Boolean.TRUE.equals(get(phoneDiagnostic, "authority"))
                        && ((Number) get(phoneDiagnostic, "readers")).intValue() == 1,
                "root diagnostic role/readers");
        check(
                ((Number) get(phoneDiagnostic, "hfp_listeners")).intValue() == 1
                        && ((Number) get(phoneDiagnostic, "hfp_last_state")).intValue() == 128,
                "root local HFP diagnostics");
        check(
                ((Number) get(get(phoneDiagnostic, "events"), "READ_SUBSCRIBE")).intValue() >= 1,
                "root diagnostic subscribe counter");
        check(
                Boolean.FALSE.equals(get(mirrorDiagnostic, "authority"))
                        && Boolean.TRUE.equals(get(mirrorDiagnostic, "bound")),
                "mirror diagnostic bound role");
        check(
                ((Number) get(get(mirrorDiagnostic, "events"), "BIND_OK")).intValue() >= 1
                        && ((Number) get(get(mirrorDiagnostic, "events"), "MIRROR_ACCEPT"))
                                        .intValue()
                                >= 1,
                "mirror diagnostic bind/delivery counters");
    }

    private static void checkLateAndDuplicate(
            Class<?> runtime, Class<?> lp, Object client, String active, String incoming)
            throws Exception {
        int before = Integer.parseInt((String) invoke(lp, client, "callbackCount"));

        invoke(lp, client, "lateListener");
        check(
                Integer.parseInt((String) invoke(lp, client, "callbackCount")) == before + 1,
                "late registration must actually emit one callback");
        check(("128:" + NUMBER).equals(invoke(lp, client, "lastCall")), "late active state");
        invoke(lp, client, "mirrorMutationsRejected");
        before = Integer.parseInt((String) invoke(lp, client, "callbackCount"));
        send(runtime, active, 1000, TOKEN, 1, false);
        send(runtime, incoming, 1000, TOKEN, 1, false);
        invoke(lp, client, "connected");
        check(
                Integer.parseInt((String) invoke(lp, client, "callbackCount")) == before,
                "duplicate/out-of-order snapshot callback");
    }

    private static void rejectMaliciousBinder(
            URLClassLoader boundary, Class<?> runtime, String active) throws Exception {
        for (int bad = 0; bad < 4; bad++) {
            int rejectedBefore =
                    ((java.util.concurrent.atomic.AtomicInteger)
                                    boundary.loadClass("android.os.Binder")
                                            .getField("REMOTE_REJECTIONS")
                                            .get(null))
                            .get();
            boolean rejected = false;

            try {
                send(
                        runtime,
                        active,
                        bad == 0 ? 1001 : 1000,
                        bad == 1 ? "wrong" : TOKEN,
                        bad == 2 ? 2 : 1,
                        bad == 3);
            } catch (SecurityException expected) {
                rejected = true;
            }

            if (!rejected
                    && ((java.util.concurrent.atomic.AtomicInteger)
                                            boundary.loadClass("android.os.Binder")
                                                    .getField("REMOTE_REJECTIONS")
                                                    .get(null))
                                    .get()
                            != rejectedBefore + 1) {
                throw new AssertionError("malicious Binder accepted " + bad);
            }
        }
    }

    private static void runDeath(
            URLClassLoader boundary,
            Class<?> runtime,
            Class<?> lp,
            Object client,
            URL classes,
            String active)
            throws Exception {
        // Queue a valid packet, revoke authority before main handles it: it must never
        // revive old UI.
        send(runtime, active, 1000, TOKEN, 1, false);
        runtime.getMethod("killAuthority").invoke(null);
        check(
                Boolean.FALSE.equals(invoke(lp, client, "connected")),
                "authority death retains stale connection");
        check(
                "131:".equals(invoke(lp, client, "lastCall")),
                "authority death retains stale active call");

        try (URLClassLoader restarted = new URLClassLoader(new URL[] {classes}, boundary)) {
            Class<?> rp = restarted.loadClass("fixture.PhonePeerProcessProbe");
            Object next = rp.getMethod("create", String.class).invoke(null, "phone");
            invoke(rp, next, "control", "connect", "{}");
            invoke(rp, next, "control", "incoming", "{\"number\":\"+70000000002\"}");
            expect(
                    lp,
                    client,
                    "lastCall",
                    "130:+70000000002",
                    "new authority incarnation after death must replace old epoch");
        }
    }

    private static void runMalformed(
            URLClassLoader boundary,
            Class<?> runtime,
            Class<?> lp,
            Object client,
            String active,
            String scenario)
            throws Exception {
        Class<?> json = boundary.loadClass("org.json.JSONObject");
        Object packet = json.getConstructor(String.class).newInstance(active);

        json.getMethod("put", String.class, long.class).invoke(packet, "sequence", 100L);
        String defect = scenario.substring("malformed:".length());

        if ("key".equals(defect)) {
            json.getMethod("remove", String.class).invoke(packet, "events");
            json.getMethod("put", String.class, Object.class).invoke(packet, "unknown", true);
        } else if ("sync".equals(defect)) {
            json.getMethod("put", String.class, Object.class).invoke(packet, "sync_state", "97");
        } else if ("events".equals(defect)) {
            json.getMethod("put", String.class, Object.class).invoke(packet, "events", true);
        } else {
            throw new IllegalArgumentException(defect);
        }

        send(runtime, packet.toString(), 1000, TOKEN, 1, false);
        check(
                Boolean.FALSE.equals(invoke(lp, client, "connected")),
                "malformed " + defect + " packet retained active connection");
        check(
                "131:".equals(invoke(lp, client, "lastCall")),
                "malformed packet retained active call");
    }

    private static void runPersistence(
            URLClassLoader boundary,
            Class<?> pp,
            Object authority,
            Class<?> lp,
            Object client,
            URL classes)
            throws Exception {
        invoke(pp, authority, "control", "hangup", "{}");
        expect(lp, client, "lastCall", "131:" + NUMBER, "Launcher terminated callback");
        invoke(pp, authority, "control", "disconnect", "{}");
        boundary.loadClass("android.util.AtomicFile")
                .getField("failNextWrite")
                .setBoolean(null, true);
        String beforeFailure = (String) invoke(pp, authority, "control", "snapshot", "{}");
        String configuration =
                "{\"peer_name\":\"Тестовый телефон\",\"contacts\":[{\"name\":\"Иван"
                        + " Иванов\",\"number\":\"+70000000001\"},{\"name\":\"Анна"
                        + " Петрова\",\"number\":\"+70000000002\"}]}";

        try {
            invoke(pp, authority, "control", "configure", configuration);
            throw new AssertionError("failed persistence acknowledged");
        } catch (java.io.IOException expected) {
            // Rejection is the expected outcome.
        }

        check(
                wire(boundary, beforeFailure)
                        .equals(
                                wire(
                                        boundary,
                                        (String)
                                                invoke(
                                                        pp,
                                                        authority,
                                                        "control",
                                                        "snapshot",
                                                        "{}"))),
                "failed persistence mutated live peer");
        invoke(pp, authority, "control", "configure", configuration);
        checkRestored(boundary, classes);
    }

    private static void checkRestored(URLClassLoader boundary, URL classes) throws Exception {
        try (URLClassLoader restarted = new URLClassLoader(new URL[] {classes}, boundary)) {
            Class<?> rp = restarted.loadClass("fixture.PhonePeerProcessProbe");
            Object recovered = rp.getMethod("create", String.class).invoke(null, "phone");
            String restored = (String) invoke(rp, recovered, "control", "snapshot", "{}");
            Class<?> json = boundary.loadClass("org.json.JSONObject");
            Object data = json.getConstructor(String.class).newInstance(restored);
            check(
                    "Тестовый телефон"
                            .equals(
                                    json.getMethod("getString", String.class)
                                            .invoke(data, "peer_name")),
                    "configured name not restored");
            check(
                    Boolean.FALSE.equals(
                            json.getMethod("getBoolean", String.class).invoke(data, "connected")),
                    "restored configuration invents connection");
            Object call = json.getMethod("getJSONObject", String.class).invoke(data, "call");
            check(
                    Integer.valueOf(131)
                            .equals(json.getMethod("getInt", String.class).invoke(call, "state")),
                    "restored configuration invents active call");
            check(
                    "".equals(json.getMethod("getString", String.class).invoke(call, "number")),
                    "restored configuration persists live number");
            check(
                    restored.contains("Иван Иванов")
                            && restored.contains("Анна Петрова")
                            && restored.contains("+70000000001")
                            && restored.contains("+70000000002"),
                    "configured two Russian contacts not restored");
        }
    }

    private static Object json(ClassLoader boundary, String text) throws Exception {
        return boundary.loadClass("org.json.JSONObject")
                .getConstructor(String.class)
                .newInstance(text);
    }

    private static Object get(Object object, String key) throws Exception {
        return object.getClass().getMethod("get", String.class).invoke(object, key);
    }

    private static String wire(ClassLoader boundary, String privileged) throws Exception {
        Object object = json(boundary, privileged);

        check(
                ((Number) object.getClass().getMethod("length").invoke(object)).intValue() == 13,
                "root diagnostic snapshot outer schema");
        Object diagnostics =
                object.getClass().getMethod("remove", String.class).invoke(object, "diagnostics");
        check(((Number) get(diagnostics, "schema")).intValue() == 1, "root diagnostic schema");
        String text = object.toString();
        assertWire(boundary, text);

        return text;
    }

    private static void assertWire(ClassLoader boundary, String text) throws Exception {
        Object object = json(boundary, text);

        check(
                ((Number) object.getClass().getMethod("length").invoke(object)).intValue() == 12,
                "wire must stay12fields");
        check(
                Boolean.FALSE.equals(
                        object.getClass()
                                .getMethod("has", String.class)
                                .invoke(object, "diagnostics")),
                "privileged diagnostics leaked into wire");
        Object call = get(object, "call");
        check(
                ((Number) call.getClass().getMethod("length").invoke(call)).intValue() == 5,
                "wire call must stay5fields");
    }

    private static void assertLogs(ClassLoader boundary, int maximum) throws Exception {
        boundary.loadClass("android.util.Log")
                .getMethod("assertSafe", int.class)
                .invoke(null, maximum);
    }

    private static void send(
            Class<?> runtime, String snapshot, int uid, String token, int code, boolean trailing)
            throws Exception {
        try {
            runtime.getMethod(
                            "send",
                            String.class,
                            int.class,
                            int.class,
                            String.class,
                            int.class,
                            boolean.class)
                    .invoke(null, snapshot, uid, 4101, token, code, trailing);
        } catch (InvocationTargetException failure) {
            rethrow(failure);
        }
    }

    private static void expect(
            Class<?> type, Object object, String method, Object expected, String message)
            throws Exception {
        long until = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(3);

        while (!expected.equals(invoke(type, object, method)) && System.nanoTime() < until) {
            Thread.sleep(5);
        }

        check(expected.equals(invoke(type, object, method)), message);
    }

    private static Object invoke(Class<?> type, Object object, String name, String... args)
            throws Exception {
        Class<?>[] types = new Class<?>[args.length];

        java.util.Arrays.fill(types, String.class);

        try {
            return type.getMethod(name, types).invoke(object, (Object[]) args);
        } catch (InvocationTargetException failure) {
            rethrow(failure);
            throw failure;
        }
    }

    private static void rethrow(InvocationTargetException failure) throws Exception {
        Throwable cause = failure.getCause();

        if (cause instanceof Exception exception) {
            throw exception;
        }

        if (cause instanceof Error error) {
            throw error;
        }

        throw failure;
    }
}
