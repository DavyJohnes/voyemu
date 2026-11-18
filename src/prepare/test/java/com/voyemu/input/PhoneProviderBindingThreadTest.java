package com.voyemu.input;

import android.os.Bundle;

import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.HexFormat;

/**
 * Actual provider reflection; own doubles model only pinned OEM Hfp's implicit Handler requirement.
 */
public final class PhoneProviderBindingThreadTest {
    static final String TOKEN = "0123456789abcdef0123456789abcdef";

    static final class Provider extends PhoneControlProvider {
        @Override
        protected String readBoot() {
            return "00000000-0000-0000-0000-000000000001";
        }
    }

    static Bundle bootstrap() {
        Bundle b = request(0);

        b.put("reset", false);

        return b;
    }

    static Bundle request(long sequence) {
        Bundle b = new Bundle();

        b.putInt("schema", 1);
        b.putString("session", "thread_boundary");
        b.putString("token", TOKEN);
        b.putString("boot", "00000000-0000-0000-0000-000000000001");
        b.putLong("sequence", sequence);

        return b;
    }

    static String call(Provider provider, String op, Bundle request) {
        return provider.call(op, null, request).getString("json");
    }

    static void need(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    static void fresh() {
        com.qinggan.bluetooth.BTManager.freshProcessBoundary();
        android.os.Looper.leave();
        android.os.Handler.hold = false;
    }

    public static void main(String[] args) throws Exception {
        Path smali = Path.of(args[1]);
        String actual =
                HexFormat.of()
                        .formatHex(
                                MessageDigest.getInstance("SHA-256")
                                        .digest(Files.readAllBytes(smali)));

        need(actual.equals(args[2]));
        need(
                Files.readString(smali)
                        .contains("Landroid/os/Handler;-><init>(Landroid/os/Handler$Callback;)V"));

        try (URLClassLoader factory =
                new URLClassLoader(
                        new java.net.URL[] {Path.of(args[0]).toUri().toURL()},
                        PhoneProviderBindingThreadTest.class.getClassLoader())) {
            com.qinggan.bluetooth.BTManager.factoryLoader = factory;
            fresh();

            if (args[3].equals("old")) {
                String failed = call(new Provider(), "bootstrap", bootstrap());
                need(
                        failed.contains("PHONE_ADAPTER_UNAVAILABLE")
                                && failed.contains("\"mutation_started\":false"));
                need(com.qinggan.bluetooth.BTManager.factoryFailures == 1);
                android.os.Looper.enterMain();
                need(
                        call(new Provider(), "bootstrap", bootstrap())
                                .contains("PHONE_ADAPTER_UNAVAILABLE"));
                com.qinggan.bluetooth.BTManager.freshProcessBoundary();
                need(call(new Provider(), "bootstrap", bootstrap()).contains("\"ok\":true"));
                System.out.println(
                        "PASS old actual provider reproduces Binder/no-Looper factory failure,"
                                + " cached null remains, fresh main initialization succeeds");

                return;
            }

            String success = call(new Provider(), "bootstrap", bootstrap());
            need(success.contains("\"ok\":true"));
            need(com.qinggan.bluetooth.BTManager.factoryFailures == 0);
            Object manager =
                    com.qinggan.bluetooth.BTManager.getInstance(new android.content.Context());
            var field = manager.getClass().getDeclaredField("mAdapter");
            field.setAccessible(true);
            need(field.get(manager).getClass().getClassLoader() == factory);
            Class<?> adapter = field.get(manager).getClass();
            var controls = adapter.getField("controls");
            fresh();
            android.os.Looper.enterMain();
            need(call(new Provider(), "bootstrap", bootstrap()).contains("\"ok\":true"));
            android.os.Looper.leave();
            fresh();
            com.qinggan.bluetooth.BTManager.getInstance(new android.content.Context());
            String cached = call(new Provider(), "bootstrap", bootstrap());
            need(cached.contains("PHONE_ADAPTER_FRESH_PROCESS_REQUIRED"));
            fresh();
            int before = controls.getInt(null);
            android.os.Handler.hold = true;
            long start = System.nanoTime();
            String timeout = call(new Provider(), "bootstrap", bootstrap());
            long elapsed = (System.nanoTime() - start) / 1000000;
            need(
                    timeout.contains("PHONE_ADAPTER_TIMEOUT")
                            && timeout.contains("\"mutation_started\":false"));
            need(elapsed >= 900 && elapsed < 2500);
            android.os.Handler.drainMain();
            need(
                    com.qinggan.bluetooth.BTManager.factoryAttempts == 0
                            && controls.getInt(null) == before);
            fresh();
            android.os.Handler.hold = true;
            Thread.currentThread().interrupt();
            String interrupted = call(new Provider(), "bootstrap", bootstrap());
            need(
                    interrupted.contains("PHONE_ADAPTER_INIT")
                            && Thread.currentThread().isInterrupted());
            Thread.interrupted();
            android.os.Handler.drainMain();
            need(
                    com.qinggan.bluetooth.BTManager.factoryAttempts == 0
                            && controls.getInt(null) == before);
            fresh();
            com.qinggan.bluetooth.BTManager.runtimeFailure = new IllegalStateException(TOKEN);
            String exception = call(new Provider(), "bootstrap", bootstrap());
            need(exception.contains("PHONE_ADAPTER_INIT") && !exception.contains(TOKEN));
            fresh();
            com.qinggan.bluetooth.BTManager.errorFailure = new AssertionError(TOKEN);
            String error = call(new Provider(), "bootstrap", bootstrap());
            need(error.contains("PHONE_ADAPTER_INIT") && !error.contains(TOKEN));
            fresh();
            Provider mutation = new Provider();
            need(call(mutation, "bootstrap", bootstrap()).contains("\"ok\":true"));
            before = controls.getInt(null);
            android.os.Handler.hold = true;
            Bundle connect = request(1);
            connect.putString("method", "connect");
            connect.putString("payload", "{}");
            String unknown = call(mutation, "control", connect);
            need(
                    unknown.contains("PHONE_ADAPTER_TIMEOUT")
                            && unknown.contains("\"mutation_started\":true"));
            android.os.Handler.drainMain();
            need(controls.getInt(null) == before);
            android.os.Handler.hold = false;
            need(call(mutation, "control", connect).contains("PHONE_UNCERTAIN"));
            System.out.println(
                    "PASS fixed actual provider: Binder main-first/existing external object,"
                        + " main-inline, cached-null fresh-process reason, deadline cancellation/no"
                        + " late peer control, interruption, exception/error confidentiality and"
                        + " uncertain mutation fencing; Hfp smali SHA="
                            + actual);
        } finally {
            Thread.interrupted();
            android.os.Handler.hold = false;
            android.os.Looper.leave();
        }
    }
}
