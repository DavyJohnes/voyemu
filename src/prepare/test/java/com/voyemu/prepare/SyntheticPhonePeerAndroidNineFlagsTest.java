package com.voyemu.prepare;

import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Path;
import java.util.Map;
import java.util.concurrent.TimeUnit;

/**
 * Android9 remote Binder supplies TF_ACCEPT_FDS to Java onTransact; local Binder preserves flags.
 */
public final class SyntheticPhonePeerAndroidNineFlagsTest {
    public static void main(String[] args) throws Exception {
        if (args.length != 3) {
            throw new IllegalArgumentException("production classes, boundary classes, SDK jar");
        }

        try (URLClassLoader boundary =
                        new URLClassLoader(
                                new URL[] {
                                    Path.of(args[1]).toUri().toURL(),
                                    Path.of(args[2]).toUri().toURL()
                                },
                                ClassLoader.getPlatformClassLoader());
                URLClassLoader phone =
                        new URLClassLoader(new URL[] {Path.of(args[0]).toUri().toURL()}, boundary);
                URLClassLoader launcher =
                        new URLClassLoader(
                                new URL[] {Path.of(args[0]).toUri().toURL()}, boundary)) {
            Class<?> pp = phone.loadClass("fixture.PhonePeerProcessProbe"),
                    lp = launcher.loadClass("fixture.PhonePeerProcessProbe");
            Object authority = pp.getMethod("create", String.class).invoke(null, "phone"),
                    client = lp.getMethod("create", String.class).invoke(null, "launcher");
            awaitIncoming(pp, authority, lp, client);
            Map<String, Object> events = Json.object(diagnostics(lp, client).get("events"));
            int flags =
                    boundary.loadClass("android.os.Binder")
                            .getField("lastRemoteFlags")
                            .getInt(null);
            System.out.println(
                    "remote_flags="
                            + flags
                            + " callback_accept="
                            + events.get("CALLBACK_ACCEPT")
                            + " callback_reject="
                            + events.get("CALLBACK_REJECT")
                            + " source_send_ok="
                            + Json.object(diagnostics(pp, authority).get("events")).get("SEND_OK"));

            if (flags != 17) {
                throw new AssertionError("Android9 TF_ACCEPT_FDS boundary prerequisite");
            }

            if (!"130:+70000000001".equals(lp.getMethod("lastCall").invoke(client))) {
                throw new AssertionError(
                        "Android9 remote one-way flags17 rejected; Launcher missing authoritative"
                                + " incoming callback");
            }

            Class<?> runtime = boundary.loadClass("android.os.PeerTestRuntime");
            Map<String, Object> packet =
                    Json.object(
                            Json.parse(
                                    (String)
                                            pp.getMethod("control", String.class, String.class)
                                                    .invoke(authority, "snapshot", "{}")));
            packet.remove("diagnostics");
            String wire = Json.write(packet);
            Thread.sleep(30);
            long accepted = accepted(lp, client);
            rejectForeignPackets(boundary, runtime, wire);

            if (accepted(lp, client) != accepted) {
                throw new AssertionError("rejected packet accepted counter changed");
            }

            send(runtime, wire, 1000, 4102, "com.voyemu.phone.peer.callback.v1", 1);

            if (boundary.loadClass("android.os.Binder").getField("lastLocalFlags").getInt(null)
                    != 1) {
                throw new AssertionError("local Binder must preserve flags1");
            }

            send(runtime, wire, 1000, 4102, "com.voyemu.phone.peer.callback.v1", 17);
            send(runtime, wire, 1000, 4101, "com.voyemu.phone.peer.callback.v1", 1);
            send(runtime, wire, 1000, 4101, "com.voyemu.phone.peer.callback.v1", 17);
            lp.getMethod("connected").invoke(client);

            if (accepted(lp, client) != accepted + 4) {
                throw new AssertionError("exact local1/17 remote17 allowlist");
            }

            System.out.println(
                    "local_flags=1 remote_flags=17 rejected_flags=0,16,2,18,unknown_high"
                            + " uid_token_rejected=true");
        }

        System.out.println("SyntheticPhonePeerAndroidNineFlagsTest PASS");
    }

    private static void awaitIncoming(Class<?> pp, Object authority, Class<?> lp, Object client)
            throws Exception {
        long until = System.nanoTime() + TimeUnit.SECONDS.toNanos(3);

        while (!Boolean.TRUE.equals(diagnostics(lp, client).get("bound"))
                && System.nanoTime() < until) {
            Thread.sleep(2);
        }

        if (!Boolean.TRUE.equals(diagnostics(lp, client).get("bound"))) {
            throw new AssertionError("subscription prerequisite");
        }

        pp.getMethod("control", String.class, String.class).invoke(authority, "connect", "{}");
        pp.getMethod("control", String.class, String.class)
                .invoke(authority, "incoming", "{\"number\":\"+70000000001\"}");

        if (!"130:+70000000001".equals(pp.getMethod("lastCall").invoke(authority))) {
            throw new AssertionError("authority incoming prerequisite");
        }

        until = System.nanoTime() + TimeUnit.SECONDS.toNanos(3);

        while (!"130:+70000000001".equals(lp.getMethod("lastCall").invoke(client))
                && System.nanoTime() < until) {
            Thread.sleep(2);
        }
    }

    private static long accepted(Class<?> lp, Object client) throws Exception {
        return ((Number) Json.object(diagnostics(lp, client).get("events")).get("CALLBACK_ACCEPT"))
                .longValue();
    }

    private static void rejectForeignPackets(ClassLoader boundary, Class<?> runtime, String wire)
            throws Exception {
        for (int sourcePid : new int[] {4101, 4102}) {
            for (int badFlags : new int[] {0, 16, 2, 18, 0x40000001}) {
                expectRejected(
                        boundary,
                        runtime,
                        wire,
                        1000,
                        sourcePid,
                        "com.voyemu.phone.peer.callback.v1",
                        badFlags);
            }
        }

        for (int bad = 0; bad < 2; bad++) {
            expectRejected(
                    boundary,
                    runtime,
                    wire,
                    bad == 0 ? 1001 : 1000,
                    4101,
                    bad == 1 ? "wrong" : "com.voyemu.phone.peer.callback.v1",
                    1);
        }
    }

    private static void expectRejected(
            ClassLoader boundary,
            Class<?> runtime,
            String snapshot,
            int uid,
            int pid,
            String token,
            int flags)
            throws Exception {
        java.util.concurrent.atomic.AtomicInteger rejected =
                (java.util.concurrent.atomic.AtomicInteger)
                        boundary.loadClass("android.os.Binder")
                                .getField("REMOTE_REJECTIONS")
                                .get(null);
        int before = rejected.get();

        try {
            send(runtime, snapshot, uid, pid, token, flags);
        } catch (SecurityException expected) {
            return;
        }

        if (rejected.get() != before + 1) {
            throw new AssertionError("unexpected flags/UID/token accepted " + flags);
        }
    }

    private static void send(
            Class<?> runtime, String snapshot, int uid, int pid, String token, int flags)
            throws Exception {
        try {
            runtime.getMethod(
                            "sendFlags",
                            String.class,
                            int.class,
                            int.class,
                            String.class,
                            int.class,
                            boolean.class,
                            int.class)
                    .invoke(null, snapshot, uid, pid, token, 1, false, flags);
        } catch (java.lang.reflect.InvocationTargetException failure) {
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

    private static Map<String, Object> diagnostics(Class<?> probe, Object value) throws Exception {
        return Json.object(
                Json.object(Json.parse((String) probe.getMethod("localSnapshot").invoke(value)))
                        .get("diagnostics"));
    }
}
