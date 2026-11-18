package com.qinggan.canbus.service;

import android.os.Bundle;

import com.qinggan.bluetooth.BTManager;
import com.qinggan.bluetooth.profile.BTAdapter;

public final class HfpPhoneInfoBridgeTest {
    private static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    private static Bundle snapshot(int state, long start) {
        Bundle b = new Bundle();

        b.put("schema", 1);
        b.put("state", state);
        b.put("number", "+70000000001");
        b.put("name", "Иван Иванов");
        b.put("active_started_elapsed_ms", start);
        b.put("connected", true);
        b.put("incarnation", "12345678-1234-1234-1234-123456789abc");

        return b;
    }

    public static void main(String[] args) {
        BTAdapter adapter = BTManager.INSTANCE.adapter;
        CanBusStubService sink = new CanBusStubService();
        HfpPhoneInfoBridge bridge =
                new HfpPhoneInfoBridge(
                        new android.content.Context(), sink, new PhoneDeliveryDiagnostics());

        bridge.start();
        bridge.start();
        check(adapter.hfp.registrations == 1);
        adapter.data = snapshot(130, 0);
        adapter.hfp.callback.on3WayCallStateChanged(130, 1, "+70000000001");
        check(
                sink.count == 1
                        && sink.last.state.name().equals("COMING_CALL")
                        && sink.last.name.equals("Иван Иванов"));
        adapter.data = snapshot(128, 1000L);
        adapter.hfp.callback.on3WayCallStateChanged(128, 1, "+70000000001");
        check(
                sink.count == 2
                        && sink.last.duration == 8
                        && sink.last.number.equals("+70000000001"));
        adapter.data.put("unexpected", 1);
        adapter.hfp.callback.on3WayCallStateChanged(128, 1, "+70000000001");
        check(sink.count == 2);
        adapter.data = snapshot(130, 0);
        adapter.hfp.callback.on3WayCallStateChanged(129, 1, "+70000000001");
        check(sink.count == 2);
        adapter.hfp.callback.on3WayCallStateChanged(130, 2, "+70000000001");
        check(sink.count == 2);
        adapter.data.put("active_started_elapsed_ms", 0);
        adapter.hfp.callback.on3WayCallStateChanged(130, 1, "+70000000001");
        check(sink.count == 2);
        adapter.data = snapshot(131, 0);
        adapter.data.put("connected", false);
        adapter.data.put("number", "");
        adapter.data.put("name", "");
        adapter.data.put("incarnation", null);
        adapter.hfp.callback.on3WayCallStateChanged(131, 1, "");
        check(
                sink.count == 3
                        && sink.last.state.name().equals("HANG_UP")
                        && sink.last.duration == 0
                        && sink.last.number.equals(""));
        adapter.hfp.callback.on3WayCallStateChanged(131, 1, "");
        check(sink.count == 3);
        adapter.data.put("incarnation", "12345678-1234-1234-1234-123456789abc");
        adapter.hfp.callback.on3WayCallStateChanged(131, 1, "");
        check(sink.count == 3);
        bridge.close();
        bridge.close();
        bridge.start();
        check(
                adapter.hfp.removals == 1
                        && adapter.hfp.registrations == 1
                        && adapter.hfp.callback == null);
        System.out.println("HfpPhoneInfoBridgeTest PASS");
    }
}
