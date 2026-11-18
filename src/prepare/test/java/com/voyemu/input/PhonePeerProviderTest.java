package com.voyemu.input;

import android.app.ActivityManager.RunningAppProcessInfo;
import android.os.Binder;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;

public final class PhonePeerProviderTest {
    static final String PHONE = "com.qinggan.bluetoothphone";

    static void need(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    public static final class Adapter {
        int calls;
        boolean bad;

        public Bundle subscribePeer(IBinder callback, int uid, int pid) {
            need(uid == 1000 && pid == 70);
            need(Looper.isMain);
            calls++;
            Bundle b = new Bundle();
            b.putInt("schema", 1);
            b.putInt("server_pid", bad ? 99 : 42);
            b.putInt("server_uid", 1000);
            b.putBinder("authority", new Binder());
            b.putString("snapshot", "{}");

            return b;
        }

        public void unsubscribePeer(IBinder callback, int uid, int pid) {
            need(uid == 1000 && pid == 70);
            need(Looper.isMain);
            calls++;
        }
    }

    static final class Provider extends PhonePeerProvider {
        Adapter adapter = new Adapter();
        boolean ready = true, slow;

        protected Object lookupAdapter() {
            need(Looper.isMain);

            if (slow) {
                try {
                    Thread.sleep(1050);
                } catch (InterruptedException error) {
                    throw new AssertionError(error);
                }
            }

            if (!ready) {
                throw new IllegalStateException();
            }

            return adapter;
        }
    }

    static Bundle request() {
        Bundle b = new Bundle();

        b.putInt("schema", 1);
        b.putBinder("callback", new Binder());

        return b;
    }

    static RunningAppProcessInfo identity() {
        RunningAppProcessInfo p = new RunningAppProcessInfo();

        p.pid = 70;
        p.uid = 1000;
        p.processName = PHONE;
        p.pkgList = new String[] {PHONE};

        return p;
    }

    static void reset() {
        android.content.Context.manager.processes.clear();
        android.content.Context.manager.processes.add(identity());
        RunningAppProcessInfo server = identity();
        server.pid = 42;
        android.content.Context.manager.processes.add(server);
        android.content.pm.PackageManager.signature = 0;
        Binder.pid = 70;
        Binder.uid = 1000;
    }

    static void denied(Provider p) {
        int calls = p.adapter.calls;

        try {
            p.call("subscribe", null, request());
            throw new AssertionError();
        } catch (SecurityException expected) {
            need(p.adapter.calls == calls);
        }
    }

    public static void main(String[] args) throws Exception {
        reset();
        Provider p = new Provider();
        need(p.call("subscribe", null, request()).getBinder("authority") != null);
        need(p.adapter.calls == 1);
        p.call("unsubscribe", null, request());
        need(p.adapter.calls == 2);

        for (String client :
                new String[] {"com.qinggan.app.launcher", "com.qinggan.canbus.service"}) {
            RunningAppProcessInfo entry = android.content.Context.manager.processes.get(0);
            entry.processName = client;
            entry.pkgList = new String[] {client};
            p.call("subscribe", null, request());
            entry.processName = client + ":remote";
            denied(p);
            entry.processName = client + ":ct";
            denied(p);
            entry.processName = client;
            entry.pkgList = new String[] {PHONE};
            denied(p);
            entry.pkgList = new String[] {client};
            android.content.pm.PackageManager.signature = -1;
            denied(p);
            reset();
        }

        android.content.Context.manager.processes.get(1).processName = PHONE + ":secondary";
        denied(p);
        reset();
        Binder.uid = 0;
        denied(p);
        reset();
        Binder.pid = 71;
        denied(p);
        reset();
        android.content.Context.manager.processes.add(identity());
        denied(p);
        reset();
        android.content.Context.manager.processes.get(0).processName = PHONE + ":remote";
        denied(p);
        reset();
        android.content.Context.manager.processes.get(0).pkgList =
                new String[] {PHONE, "shared.uid.package"};
        denied(p);
        reset();
        android.content.pm.PackageManager.signature = -1;
        denied(p);
        reset();

        for (String operation : new String[] {"control", "snapshot", "bootstrap", "answer"}) {
            try {
                p.call(operation, null, request());
                throw new AssertionError();
            } catch (IllegalArgumentException expected) {
            }
        }

        Bundle malformed = request();
        malformed.put("schema", Long.valueOf(1));

        try {
            p.call("subscribe", null, malformed);
            throw new AssertionError();
        } catch (IllegalArgumentException expected) {
        }

        malformed = request();
        malformed.putBinder(
                "callback",
                new IBinder() {
                    public boolean isBinderAlive() {
                        return false;
                    }
                });

        try {
            p.call("subscribe", null, malformed);
            throw new AssertionError();
        } catch (IllegalArgumentException expected) {
        }

        Bundle extra = request();
        extra.putString("method", "answer");

        try {
            p.call("subscribe", null, extra);
            throw new AssertionError();
        } catch (IllegalArgumentException expected) {
        }

        p.ready = false;

        try {
            p.call("subscribe", null, request());
            throw new AssertionError();
        } catch (IllegalStateException expected) {
        }

        p.ready = true;
        p.call("subscribe", null, request());
        int before = p.adapter.calls;
        Handler.hold = true;

        try {
            p.call("subscribe", null, request());
            throw new AssertionError();
        } catch (IllegalStateException expected) {
        } finally {
            Handler.hold = false;
        }

        Looper.isMain = true;
        Handler.queued.run();
        Looper.isMain = false;
        need(p.adapter.calls == before);
        p.slow = true;
        before = p.adapter.calls;

        try {
            p.call("subscribe", null, request());
            throw new AssertionError();
        } catch (IllegalStateException expected) {
        }

        p.slow = false;
        need(p.adapter.calls == before);
        p.adapter.bad = true;

        try {
            p.call("subscribe", null, request());
            throw new AssertionError();
        } catch (IllegalStateException expected) {
        }

        checkDiagnostics(p);
        System.out.println(
                "PASS max64 fixed numeric diagnostics/no sensitive fields; captured exact"
                    + " process/signature identity, read-only request grammar, cold adapter retry,"
                    + " main dispatch, deadline cancellation, server binding");
    }

    private static void checkDiagnostics(Provider p) throws Exception {
        need(!PhonePeerProvider.allowedIdentity(PHONE, new String[] {PHONE, PHONE}));
        java.lang.reflect.Field counter = PhonePeerProvider.class.getDeclaredField("DIAGNOSTICS");
        counter.setAccessible(true);
        ((java.util.concurrent.atomic.AtomicInteger) counter.get(null)).set(0);
        android.util.Log.ENTRIES.clear();
        reset();
        p.adapter.bad = false;
        p.call("subscribe", null, request());
        android.content.Context.manager.processes.get(1).processName = "SECRET_SERVER";
        denied(p);
        reset();
        android.content.Context.manager.processes.get(0).processName = "SECRET_CALLER";
        denied(p);
        reset();
        android.content.pm.PackageManager.signature = -1;
        denied(p);
        reset();

        for (int i = 0; i < 100; i++) {
            Bundle secret = request();
            secret.putString("SECRET_TOKEN", "+70000000001 SECRET_NAME");

            try {
                p.call("SECRET_OPERATION", null, secret);
                throw new AssertionError();
            } catch (IllegalArgumentException expected) {
            }
        }

        need(android.util.Log.ENTRIES.size() == 64);

        for (String entry : android.util.Log.ENTRIES) {
            need(
                    entry.matches(
                            "VoyemuPhonePeer code=(?:[1-6]|2[0-7])(?: uid=-?[0-9]+"
                                    + " pid=-?[0-9]+)?"));
        }

        need(android.util.Log.ENTRIES.contains("VoyemuPhonePeer code=1 uid=1000 pid=70"));
        need(
                android.util.Log.ENTRIES.contains("VoyemuPhonePeer code=20")
                        && android.util.Log.ENTRIES.contains("VoyemuPhonePeer code=21")
                        && android.util.Log.ENTRIES.contains("VoyemuPhonePeer code=22"));
    }
}
