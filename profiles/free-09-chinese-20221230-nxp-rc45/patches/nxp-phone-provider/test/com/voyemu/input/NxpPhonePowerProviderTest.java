package com.voyemu.input;

import android.app.ActivityManager.RunningAppProcessInfo;
import android.os.Binder;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;

public final class NxpPhonePowerProviderTest {
    static final String PHONE = "com.qinggan.bluetoothphone", SETTINGS = "com.qinggan.app.setting";
    static int checks;

    static void need(boolean value) {
        checks++;

        if (!value) {
            throw new AssertionError("check " + checks);
        }
    }

    public static final class Adapter {
        int calls;
        boolean enabled;

        public void setPowerOnHandler(boolean value) {
            need(Looper.isMain);
            calls++;
            enabled = value;
        }
    }

    static final class Provider extends PhonePeerProvider {
        Adapter adapter = new Adapter();
        boolean slow;

        protected Object lookupAdapter() {
            need(Looper.isMain);

            if (slow) {
                try {
                    Thread.sleep(1050);
                } catch (InterruptedException error) {
                    throw new AssertionError(error);
                }
            }

            return adapter;
        }
    }

    static RunningAppProcessInfo identity(String name, int pid) {
        RunningAppProcessInfo p = new RunningAppProcessInfo();

        p.pid = pid;
        p.uid = 1000;
        p.processName = name;
        p.pkgList = new String[] {name};

        return p;
    }

    static void reset() {
        android.content.Context.manager.processes.clear();
        android.content.Context.manager.processes.add(identity(SETTINGS, 70));
        android.content.Context.manager.processes.add(identity(PHONE, 42));
        android.content.pm.PackageManager.signature = 0;
        Binder.pid = 70;
        Binder.uid = 1000;
    }

    static Bundle request(boolean value) {
        Bundle b = new Bundle();

        b.putInt("schema", 1);
        b.putBoolean("enabled", value);

        return b;
    }

    static void denied(Provider p) {
        int before = p.adapter.calls;

        try {
            p.call("set_power", null, request(true));
            throw new AssertionError("unauthorized");
        } catch (SecurityException expected) {
            need(before == p.adapter.calls);
        }
    }

    public static void main(String[] args) throws Exception {
        reset();
        Provider p = new Provider();
        Bundle reply = p.call("set_power", null, request(true));
        need(p.adapter.enabled && p.adapter.calls == 1 && reply.getBoolean("enabled"));
        p.call("set_power", null, request(false));
        need(!p.adapter.enabled && p.adapter.calls == 2);

        for (String name :
                new String[] {
                    PHONE,
                    "com.qinggan.app.launcher",
                    "com.qinggan.canbus.service",
                    SETTINGS + ":remote",
                    "other"
                }) {
            android.content.Context.manager.processes.set(0, identity(name, 70));
            denied(p);
            reset();
        }

        Binder.uid = 0;
        denied(p);
        reset();
        Binder.pid = 71;
        denied(p);
        reset();
        android.content.Context.manager.processes.add(identity(SETTINGS, 70));
        denied(p);
        reset();
        android.content.Context.manager.processes.get(0).pkgList = new String[] {SETTINGS, PHONE};
        denied(p);
        reset();
        android.content.pm.PackageManager.signature = -1;
        denied(p);
        reset();
        android.content.Context.manager.processes.get(1).processName = PHONE + ":remote";
        denied(p);
        reset();

        for (Object invalid : new Object[] {null, 1, "true", Long.valueOf(1)}) {
            Bundle b = request(true);
            b.put("enabled", invalid);
            int before = p.adapter.calls;

            try {
                p.call("set_power", null, b);
                throw new AssertionError("invalid enabled");
            } catch (IllegalArgumentException expected) {
                need(before == p.adapter.calls);
            }
        }

        Bundle b = request(true);
        b.putString("extra", "x");

        try {
            p.call("set_power", null, b);
            throw new AssertionError();
        } catch (IllegalArgumentException expected) {
            need(true);
        }

        b = request(true);
        b.put("schema", Long.valueOf(1));

        try {
            p.call("set_power", null, b);
            throw new AssertionError();
        } catch (IllegalArgumentException expected) {
            need(true);
        }

        try {
            p.call("set_power", "x", request(true));
            throw new AssertionError();
        } catch (IllegalArgumentException expected) {
            need(true);
        }

        int before = p.adapter.calls;
        Handler.hold = true;

        try {
            p.call("set_power", null, request(true));
            throw new AssertionError();
        } catch (IllegalStateException expected) {
            need(p.adapter.calls == before);
        } finally {
            Handler.hold = false;
        }

        Looper.isMain = true;
        Handler.queued.run();
        Looper.isMain = false;
        need(p.adapter.calls == before);
        p.slow = true;

        try {
            p.call("set_power", null, request(true));
            throw new AssertionError();
        } catch (IllegalStateException expected) {
            need(p.adapter.calls == before);
        }

        System.out.println(
                "NXP power provider PASS "
                        + checks
                        + " (identity, signer, schema, queue cancellation, expired lookup)");
    }
}
