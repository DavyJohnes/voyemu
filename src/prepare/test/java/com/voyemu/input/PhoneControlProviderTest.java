package com.voyemu.input;

import android.os.Bundle;

import org.json.JSONObject;

/** Execute production call(), replacing only Android boot I/O and the OEM callback. */
public final class PhoneControlProviderTest {
    static final String BOOT = "00000000-0000-0000-0000-000000000001",
            TOKEN = "0123456789abcdef0123456789abcdef";

    static final class Probe extends PhoneControlProvider {
        int calls, mutations;
        boolean fail, failDisconnect, connected;
        Exception injected;
        int callState = 131;

        @Override
        protected String readBoot() {
            return BOOT;
        }

        @Override
        protected JSONObject adapter(String method, String payload) throws Exception {
            calls++;

            if (injected != null) {
                throw injected;
            }

            if (fail || (failDisconnect && method.equals("disconnect"))) {
                throw new IllegalStateException("uncertain callback");
            }

            if (!method.equals("snapshot")) {
                mutations++;
            }

            if (method.equals("connect")) {
                connected = true;
            }

            if (method.equals("disconnect")) {
                connected = false;
            }

            return new JSONObject()
                    .put("ok", true)
                    .put("method", method)
                    .put("connected", connected)
                    .put("pbap_connected", connected)
                    .put("call", new JSONObject().put("state", callState));
        }
    }

    static final class Blocking extends PhoneControlProvider {
        final java.util.concurrent.CountDownLatch
                entered = new java.util.concurrent.CountDownLatch(1),
                release = new java.util.concurrent.CountDownLatch(1);

        @Override
        protected String readBoot() {
            return BOOT;
        }

        @Override
        protected JSONObject adapter(String method, String payload) throws Exception {
            entered.countDown();

            if (!release.await(5, java.util.concurrent.TimeUnit.SECONDS)) {
                throw new IllegalStateException("test-bound");
            }

            return new JSONObject().put("ok", true);
        }
    }

    static Bundle request(long n) {
        Bundle b = new Bundle();

        b.putInt("schema", 1);
        b.putString("session", "session_1");
        b.putString("token", TOKEN);
        b.putString("boot", BOOT);
        b.putLong("sequence", n);

        return b;
    }

    static Bundle bootstrap() {
        Bundle b = request(0);

        b.put("reset", false);

        return b;
    }

    static Bundle takeover(boolean reset) {
        Bundle b = bootstrap();

        b.putString("session", "session_2");
        b.put("reset", reset);

        return b;
    }

    static Bundle control(long n) {
        Bundle b = request(n);

        b.putString("method", "connect");
        b.putString("payload", "{}");

        return b;
    }

    static boolean success(Probe p, String method, Bundle b) {
        return p.call(method, null, b).getString("json").contains("\"ok\":true");
    }

    static void need(boolean v) {
        if (!v) {
            throw new AssertionError();
        }
    }

    static int checks;

    static void reject(Probe p, String method, Bundle b) {
        int before = p.calls;

        need(!success(p, method, b));
        need(before == p.calls);
        checks++;
    }

    public static void main(String[] args) throws Exception {
        Probe p = new Probe();

        android.os.Binder.uid = 1000;

        try {
            p.call("bootstrap", null, request(0));
            throw new AssertionError();
        } catch (SecurityException expected) {
            checks++;
        }

        android.os.Binder.uid = 2000;
        android.content.Context.permission = -1;

        try {
            p.call("bootstrap", null, request(0));
            throw new AssertionError();
        } catch (SecurityException expected) {
            checks++;
        }

        android.content.Context.permission = 0;
        Bundle bad = bootstrap();
        bad.putString("boot", "wrong");
        reject(p, "bootstrap", bad);
        bad = bootstrap();
        bad.putString("unknown", "x");
        reject(p, "bootstrap", bad);
        // Android 9 Content.Parser maps both :i: and :l: to Long, then CallCommand uses putLong.
        Probe cli = new Probe();
        Bundle cliBootstrap = bootstrap();
        cliBootstrap.putLong("schema", Long.parseLong("1"));
        need(success(cli, "bootstrap", cliBootstrap));
        checks++;
        Bundle cliControl = control(1);
        cliControl.putLong("schema", 1);
        need(success(cli, "control", cliControl));
        checks++;
        Bundle cliSnapshot = request(1);
        cliSnapshot.putLong("schema", 1);
        need(success(cli, "snapshot", cliSnapshot));
        checks++;

        for (Object schema :
                new Object[] {
                    null,
                    "1",
                    Float.valueOf(1),
                    Double.valueOf(1),
                    Boolean.TRUE,
                    Integer.valueOf(0),
                    Integer.valueOf(2),
                    Long.valueOf(0),
                    Long.valueOf(2),
                    Long.valueOf(4294967297L)
                }) {
            bad = bootstrap();
            bad.put("schema", schema);
            reject(p, "bootstrap", bad);
        }

        bad = bootstrap();
        bad.putInt("sequence", 0);
        reject(p, "bootstrap", bad);
        bad = bootstrap();
        bad.putString("reset", "true");
        reject(p, "bootstrap", bad);
        bad = request(0);
        bad.put("reset", false);
        reject(p, "snapshot", bad);
        need(success(p, "bootstrap", bootstrap()));
        need(p.calls == 1);
        String reply = p.call("snapshot", null, request(0)).getString("json");
        need(reply.contains("\"provider_pid\":42"));
        checks++;
        bad = bootstrap();
        bad.putString("session", "other");
        reject(p, "bootstrap", bad);
        bad = request(0);
        bad.putString("token", "11111111111111111111111111111111");
        reject(p, "snapshot", bad);
        bad = request(0);
        bad.putString("method", "connect");
        reject(p, "snapshot", bad);
        bad = control(1);
        bad.putString("method", "dial");
        reject(p, "control", bad);
        bad = control(1);
        bad.putString("payload", "invalid");
        reject(p, "control", bad);
        bad = control(1);
        bad.put("reset", true);
        reject(p, "control", bad);
        need(success(p, "control", control(1)));
        checks++;
        reject(p, "control", control(1));
        reject(p, "control", control(3));
        reject(p, "bootstrap", bootstrap());
        need(success(p, "snapshot", request(1)));
        checks++;
        p.fail = true;
        int before = p.calls;
        need(!success(p, "control", control(2)));
        need(p.calls == before + 1);
        checks++;
        p.fail = false;
        reject(p, "control", control(3));
        need(success(p, "snapshot", request(2)));
        checks++;
        checkTakeover();
        checkDiagnostics();
        checkOwnerWaitAdmission();
        System.out.println(
                "PASS "
                        + checks
                        + " provider permission, identity, exact fields/types, replay, PID,"
                        + " uncertain mutation and diagnostic snapshot checks");
    }

    private static void checkTakeover() throws Exception {
        Probe resume = new Probe();

        need(success(resume, "bootstrap", bootstrap()));
        need(success(resume, "control", control(1)));
        reject(resume, "bootstrap", takeover(false));
        resume.callState = 130;
        int before = resume.mutations;
        need(!success(resume, "bootstrap", takeover(true)));
        need(resume.mutations == before);
        checks++;
        resume.callState = 131;
        need(success(resume, "bootstrap", takeover(true)));
        need(!resume.connected);
        checks++;
        Bundle reconnect = control(1);
        reconnect.putString("session", "session_2");
        need(success(resume, "control", reconnect));
        need(resume.connected);
        checks++;
        Probe timeout = new Probe();
        need(success(timeout, "bootstrap", bootstrap()));
        need(success(timeout, "control", control(1)));
        timeout.fail = true;
        need(!success(timeout, "bootstrap", takeover(true)));
        timeout.fail = false;
        // A snapshot read timeout occurs before disconnect and cannot transfer the owner.
        need(success(timeout, "snapshot", request(1)));
        checks++;
        timeout.failDisconnect = true;
        need(!success(timeout, "bootstrap", takeover(true)));
        timeout.failDisconnect = false;
        reject(timeout, "control", control(2));
        need(success(timeout, "snapshot", request(1)));
        checks++;
    }

    private static void checkDiagnostics() throws Exception {
        Probe diagnostic = new Probe();

        diagnostic.injected = new NoSuchMethodException(TOKEN);
        String rejected = diagnostic.call("bootstrap", null, bootstrap()).getString("json");
        need(
                rejected.contains("\"reason\":\"PHONE_ADAPTER_METHOD\"")
                        && rejected.contains("\"mutation_started\":false")
                        && !rejected.contains(TOKEN));
        checks++;
        diagnostic.injected = null;
        need(success(diagnostic, "bootstrap", bootstrap()));
        Bundle wrongToken = request(0);
        wrongToken.putString("token", "11111111111111111111111111111111");
        rejected = diagnostic.call("snapshot", null, wrongToken).getString("json");
        need(
                !rejected.contains("stage")
                        && !rejected.contains("reason")
                        && !rejected.contains("mutation_started"));
        checks++;
        diagnostic.injected =
                new java.lang.reflect.InvocationTargetException(new IllegalStateException(TOKEN));
        rejected = diagnostic.call("control", null, control(1)).getString("json");
        need(
                rejected.contains("PHONE_ADAPTER_INIT")
                        && rejected.contains("\"mutation_started\":true")
                        && !rejected.contains(TOKEN));
        checks++;
        Probe timed = new Probe();
        need(success(timed, "bootstrap", bootstrap()));
        timed.injected = new java.util.concurrent.TimeoutException(TOKEN);
        rejected = timed.call("control", null, control(1)).getString("json");
        need(
                rejected.contains("PHONE_ADAPTER_TIMEOUT")
                        && rejected.contains("\"mutation_started\":true")
                        && !rejected.contains(TOKEN));
        checks++;
    }

    private static void checkOwnerWaitAdmission() throws Exception {
        android.os.Binder.uid = 0;
        Probe root = new Probe();
        need(success(root, "bootstrap", bootstrap()));
        checks++;
        android.os.Binder.uid = 2000;
        Blocking blocked = new Blocking();
        java.util.concurrent.atomic.AtomicReference<Throwable> ownerFailure =
                new java.util.concurrent.atomic.AtomicReference<>();
        Thread owner =
                new Thread(() -> {
                    try {
                        blocked.call("bootstrap", null, bootstrap());
                    } catch (Throwable failure) {
                        ownerFailure.set(failure);
                    }
                });
        owner.start();
        need(blocked.entered.await(2, java.util.concurrent.TimeUnit.SECONDS));
        java.util.concurrent.atomic.AtomicBoolean rejectedBeforeMonitor =
                new java.util.concurrent.atomic.AtomicBoolean();
        android.os.Binder.uid = 1000;
        Thread invalid =
                new Thread(() -> {
                    try {
                        blocked.call("bootstrap", null, bootstrap());
                    } catch (SecurityException expected) {
                        rejectedBeforeMonitor.set(true);
                    }
                });
        invalid.start();
        boolean rejectedWhileOwnedWait;

        try {
            invalid.join(300);
            rejectedWhileOwnedWait = rejectedBeforeMonitor.get() && !invalid.isAlive();
        } finally {
            blocked.release.countDown();
            owner.join(1500);
            invalid.join(1500);
            android.os.Binder.uid = 2000;
        }

        need(
                !owner.isAlive()
                        && !invalid.isAlive()
                        && ownerFailure.get() == null
                        && rejectedWhileOwnedWait);
        checks++;
    }
}
