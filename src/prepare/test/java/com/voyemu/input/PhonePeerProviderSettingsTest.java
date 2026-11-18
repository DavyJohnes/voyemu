package com.voyemu.input;

import android.app.ActivityManager.RunningAppProcessInfo;
import android.os.Binder;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;

import org.json.JSONObject;

import java.util.Set;
import java.util.concurrent.CountDownLatch;

public final class PhonePeerProviderSettingsTest {
    static final String PHONE = "com.qinggan.bluetoothphone",
            SETTINGS = "com.qinggan.app.setting",
            ADDRESS = "02:00:00:00:00:01",
            BOOT = "11111111-1111-1111-1111-111111111111";

    static void need(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    public static final class Adapter {
        int calls;
        String action, address;
        boolean active, connected;
        IBinder reader;
        CountDownLatch entered, proceed;

        public boolean settingsPeer(
                String operation, String target, IBinder callback, int uid, int pid) {
            need(Looper.isMain);

            if (entered != null) {
                entered.countDown();

                try {
                    proceed.await();
                } catch (InterruptedException error) {
                    throw new AssertionError(error);
                }
            }

            if (uid != 1000
                    || pid != 70
                    || callback != reader
                    || !callback.isBinderAlive()
                    || active) {
                return false;
            }

            if (operation.equals("connect") && !connected) {
                calls++;
                connected = true;
                action = operation;
                address = target;

                return true;
            }

            if (operation.equals("disconnect") && connected) {
                calls++;
                connected = false;
                action = operation;
                address = target;

                return true;
            }

            return false;
        }

        public Bundle subscribePeer(IBinder callback, int uid, int pid) {
            reader = callback;
            Bundle b = new Bundle();
            b.putInt("schema", 1);
            b.putInt("server_pid", 42);
            b.putInt("server_uid", 1000);
            b.putBinder("authority", new Binder());
            b.putString("snapshot", "{}");

            return b;
        }

        public void unsubscribePeer(IBinder callback, int uid, int pid) {
            reader = null;
        }
    }

    static final class Peer extends PhonePeerProvider {
        Adapter adapter = new Adapter();

        protected Object lookupAdapter() {
            need(Looper.isMain);

            return adapter;
        }
    }

    static final class Host extends PhoneControlProvider {
        protected String readBoot() {
            return BOOT;
        }

        protected JSONObject adapter(String method, String payload) {
            JSONObject state = new JSONObject();

            state.put("ok", true);
            state.put("connected", false);
            state.put("pbap_connected", false);
            state.put("call", new JSONObject().put("state", 131));

            return state;
        }
    }

    static RunningAppProcessInfo process(String name, int pid) {
        var p = new RunningAppProcessInfo();

        p.pid = pid;
        p.uid = 1000;
        p.processName = name;
        p.pkgList = new String[] {name};

        return p;
    }

    static void identity() {
        android.content.Context.manager.processes.clear();
        android.content.Context.manager.processes.add(process(SETTINGS, 70));
        android.content.Context.manager.processes.add(process(PHONE, 42));
        android.content.pm.PackageManager.signature = 0;
        android.content.pm.PackageManager.packageUid = 1000;
        Binder.pid = 70;
        Binder.uid = 1000;
    }

    static Bundle request(IBinder callback) {
        Bundle b = new Bundle();

        b.putInt("schema", 1);
        b.putString("address", ADDRESS);
        b.putBinder("callback", callback);

        return b;
    }

    static Bundle host(String op, long seq) {
        Bundle b = new Bundle();

        b.putInt("schema", 1);
        b.putString("session", "host");
        b.putString("token", "0123456789abcdef0123456789abcdef");
        b.putString("boot", BOOT);
        b.putLong("sequence", seq);

        if (op.equals("bootstrap")) {
            b.putBoolean("reset", false);
        }

        return b;
    }

    interface Check {
        void run() throws Exception;
    }

    static void denied(Check action) throws Exception {
        try {
            action.run();
        } catch (SecurityException | IllegalArgumentException | IllegalStateException expected) {
            return;
        }

        throw new AssertionError("accepted");
    }

    public static void main(String[] args) throws Exception {
        identity();
        Peer p = new Peer();
        Binder callback = new Binder();
        Bundle sub = new Bundle();
        sub.putInt("schema", 1);
        sub.putBinder("callback", callback);
        need(p.call("subscribe", null, sub).getBinder("authority") != null);
        Bundle reply = p.call("settings.connect", null, request(callback));
        need(
                reply.keySet().equals(Set.of("schema", "ok"))
                        && Integer.valueOf(1).equals(reply.get("schema"))
                        && Boolean.TRUE.equals(reply.get("ok")));
        need(
                p.adapter.calls == 1
                        && p.adapter.action.equals("connect")
                        && p.adapter.address.equals(ADDRESS));
        p.adapter.active = true;
        need(
                Boolean.FALSE.equals(
                        p.call("settings.disconnect", null, request(callback)).get("ok")));
        need(p.adapter.calls == 1);
        p.adapter.active = false;
        need(
                Boolean.FALSE.equals(
                        p.call("settings.disconnect", null, request(new Binder())).get("ok")));
        need(p.adapter.calls == 1);
        need(Boolean.TRUE.equals(p.call("settings.disconnect", null, request(callback)).get("ok")));
        need(p.adapter.calls == 2 && !p.adapter.connected);
        int calls = p.adapter.calls;
        Bundle wrong = request(callback);
        wrong.putString("address", "02:00:00:00:00:02");
        denied(() -> p.call("settings.connect", null, wrong));
        Bundle extra = request(callback);
        extra.putString("extra", "x");
        denied(() -> p.call("settings.connect", null, extra));
        denied(() -> p.call("settings.connect", "non-null", request(callback)));
        need(p.adapter.calls == calls);
        android.content.Context.manager.processes.get(0).processName = SETTINGS + ":remote";
        denied(() -> p.call("settings.connect", null, request(callback)));
        identity();
        android.content.Context.manager.processes.get(0).pkgList = new String[] {SETTINGS, PHONE};
        denied(() -> p.call("settings.connect", null, request(callback)));
        identity();
        android.content.pm.PackageManager.signature = -1;
        denied(() -> p.call("settings.connect", null, request(callback)));
        identity();
        Binder.pid = 71;
        denied(() -> p.call("settings.connect", null, request(callback)));
        identity();
        android.content.pm.PackageManager.packageUid = 1001;
        denied(() -> p.call("settings.connect", null, request(callback)));
        identity();
        Host host = new Host();
        Binder.uid = 2000;
        need(
                host.call("bootstrap", null, host("bootstrap", 0))
                        .getString("json")
                        .contains("\"ok\":true"));
        identity();
        need(Boolean.TRUE.equals(p.call("settings.connect", null, request(callback)).get("ok")));
        Binder.uid = 2000;
        need(
                host.call("snapshot", null, host("snapshot", 0))
                        .getString("json")
                        .contains("\"ok\":true"));
        identity();
        CountDownLatch started = new CountDownLatch(1), release = new CountDownLatch(1);
        Thread holder =
                new Thread(() -> {
                    synchronized (PhoneControlProvider.AUTHORITY_LOCK) {
                        started.countDown();

                        try {
                            release.await();
                        } catch (InterruptedException e) {
                            throw new AssertionError(e);
                        }
                    }
                });
        holder.start();
        started.await();
        Thread pending = new Thread(() -> p.call("settings.disconnect", null, request(callback)));
        pending.start();
        Thread.sleep(50);
        need(pending.isAlive());
        release.countDown();
        holder.join();
        pending.join();
        need(!p.adapter.connected);
        int queuedBefore = p.adapter.calls;
        Handler.hold = true;
        denied(() -> p.call("settings.connect", null, request(callback)));
        Handler.hold = false;
        Binder.uid = 2000;
        need(
                host.call("snapshot", null, host("snapshot", 0))
                        .getString("json")
                        .contains("\"ok\":true"));
        Looper.isMain = true;
        Handler.queued.run();
        Looper.isMain = false;
        need(p.adapter.calls == queuedBefore);
        identity();
        denied(() -> p.call("settings.connect", null, request(callback)));
        var uncertain = PhonePeerProvider.class.getDeclaredField("settingsUncertain");
        uncertain.setAccessible(true);
        uncertain.setBoolean(null, false);
        int before = p.adapter.calls;
        p.adapter.entered = new CountDownLatch(1);
        p.adapter.proceed = new CountDownLatch(1);
        Handler.async = true;
        Thread timed =
                new Thread(() -> {
                    try {
                        p.call("settings.connect", null, request(callback));
                        throw new AssertionError("timeout accepted");
                    } catch (IllegalStateException expected) {
                    }
                });
        timed.start();
        p.adapter.entered.await();
        timed.join(2500);
        need(!timed.isAlive());
        Binder.uid = 2000;
        need(
                host.call("snapshot", null, host("snapshot", 0))
                        .getString("json")
                        .contains("\"ok\":false"));
        need(p.adapter.calls == before);
        p.adapter.proceed.countDown();
        Handler.worker.join();
        Handler.async = false;
        need(p.adapter.calls == before + 1);
        need(
                host.call("snapshot", null, host("snapshot", 0))
                        .getString("json")
                        .contains("\"ok\":true"));
        identity();
        denied(() -> p.call("settings.disconnect", null, request(callback)));
    }
}
