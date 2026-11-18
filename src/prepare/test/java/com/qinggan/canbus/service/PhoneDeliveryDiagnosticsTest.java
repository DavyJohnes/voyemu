package com.qinggan.canbus.service;

import com.qinggan.bluetooth.BTManager;
import com.qinggan.bluetooth.profile.BTAdapter;

import java.util.Map;

public final class PhoneDeliveryDiagnosticsTest {
    private static void check(boolean v) {
        if (!v) {
            throw new AssertionError();
        }
    }

    @SuppressWarnings("unchecked")
    private static int count(PhoneDeliveryDiagnostics d, String key) {
        return (Integer) ((Map<String, Object>) d.snapshot().get("counts")).get(key);
    }

    private static android.os.Bundle incoming() {
        android.os.Bundle b = new android.os.Bundle();

        b.put("schema", 1);
        b.put("state", 130);
        b.put("number", "+70000000001");
        b.put("name", "Иван Иванов");
        b.put("active_started_elapsed_ms", 0L);
        b.put("connected", true);
        b.put("incarnation", "12345678-1234-1234-1234-123456789abc");

        return b;
    }

    public static void main(String[] args) throws Exception {
        PhoneDeliveryDiagnostics missing = new PhoneDeliveryDiagnostics();
        android.content.Context absent =
                new android.content.Context() {
                    public ClassLoader getClassLoader() {
                        return new ClassLoader(null) {};
                    }
                };

        new HfpPhoneInfoBridge(absent, new CanBusStubService(), missing).start();
        check(
                count(missing, "attach_attempt") == 1
                        && count(missing, "attach_success") == 0
                        && count(missing, "closed") == 1);
        check(missing.snapshot().get("first_failure").equals("attach_reflection"));
        BTAdapter a = BTManager.INSTANCE.adapter;
        a.data = incoming();
        a.emitDuringRegister = true;
        PhoneDeliveryDiagnostics d = new PhoneDeliveryDiagnostics();
        CanBusStubService sink = new CanBusStubService();
        HfpPhoneInfoBridge bridge = new HfpPhoneInfoBridge(new android.content.Context(), sink, d);
        bridge.start();
        check(
                sink.count == 1
                        && count(d, "callback_seen") == 1
                        && count(d, "metadata_accepted") == 1
                        && count(d, "sink_returned") == 1
                        && count(d, "attach_success") == 1);
        a.emitDuringRegister = false;
        a.data.put("unexpected", "must not leak");
        a.hfp.callback.on3WayCallStateChanged(130, 1, "+70000000001");
        check(
                count(d, "metadata_rejected") == 1
                        && d.snapshot().get("first_failure").equals("metadata_shape"));
        a.data = incoming();
        a.throwSnapshot = true;
        a.hfp.callback.on3WayCallStateChanged(130, 1, "+70000000001");
        a.throwSnapshot = false;
        check(d.snapshot().get("last_failure").equals("metadata_invocation"));
        a.hfp.callback.on3WayCallStateChanged(130, 2, "+70000000001");
        check(count(d, "callback_rejected") == 1);
        a.data.put("state", 129);
        a.hfp.callback.on3WayCallStateChanged(130, 1, "+70000000001");
        check(d.snapshot().get("last_failure").equals("metadata_values"));
        a.data = incoming();
        a.data.put("state", 128);
        a.data.put("active_started_elapsed_ms", 1000L);
        sink.reject = true;
        a.hfp.callback.on3WayCallStateChanged(128, 1, "+70000000001");
        check(
                d.snapshot().get("last_failure").equals("sink_runtime")
                        && count(d, "sink_rejected") == 1
                        && count(d, "sink_returned") == 1);
        check(
                !d.snapshot().toString().contains("70000000001")
                        && !d.snapshot().toString().contains("Иван")
                        && !d.snapshot().toString().contains("not included"));
        bridge.close();
        PhoneDeliveryDiagnostics concurrent = new PhoneDeliveryDiagnostics();
        Thread[] threads = new Thread[4];

        for (int i = 0; i < threads.length; i++) {
            threads[i] =
                    new Thread(() -> {
                        for (int j = 0; j < 10000; j++) {
                            concurrent.event(PhoneDeliveryDiagnostics.Event.CALLBACK_SEEN);
                        }
                    });
            threads[i].start();
        }

        for (Thread t : threads) {
            t.join();
        }

        check(count(concurrent, "callback_seen") == 40000);
        java.lang.reflect.Field counts = PhoneDeliveryDiagnostics.class.getDeclaredField("counts");
        counts.setAccessible(true);
        ((int[]) counts.get(concurrent))[PhoneDeliveryDiagnostics.Event.CALLBACK_SEEN.ordinal()] =
                Integer.MAX_VALUE;
        concurrent.event(PhoneDeliveryDiagnostics.Event.CALLBACK_SEEN);
        check(count(concurrent, "callback_seen") == Integer.MAX_VALUE);
        System.out.println("PhoneDeliveryDiagnosticsTest PASS");
    }
}
