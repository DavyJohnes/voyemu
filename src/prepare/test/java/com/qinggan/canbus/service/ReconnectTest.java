package com.qinggan.canbus.service;

import android.os.Bundle;

import com.qinggan.bluetooth.BTManager;
import com.qinggan.bluetooth.profile.BTAdapter;

public final class ReconnectTest {
    static void check(boolean v) {
        if (!v) {
            throw new AssertionError();
        }
    }

    static Bundle snapshot(int s, String epoch) {
        Bundle b = new Bundle();

        b.put("schema", 1);
        b.put("state", s);
        b.put("number", s == 131 ? "" : "+79990000001");
        b.put("name", s == 131 ? "" : "Иван Петров");
        b.put("active_started_elapsed_ms", 0L);
        b.put("connected", s != 131);
        b.put("incarnation", epoch);

        return b;
    }

    public static void main(String[] args) {
        BTAdapter a = BTManager.INSTANCE.adapter;
        CanBusStubService sink = new CanBusStubService();
        HfpPhoneInfoBridge b =
                new HfpPhoneInfoBridge(
                        new android.content.Context(), sink, new PhoneDeliveryDiagnostics());

        b.start();
        String e = "12345678-1234-1234-1234-123456789abc";
        a.data = snapshot(130, e);
        a.hfp.callback.on3WayCallStateChanged(130, 1, "+79990000001");
        check(sink.count == 1);
        a.data = snapshot(131, null);
        a.hfp.callback.on3WayCallStateChanged(131, 1, "");
        check(sink.count == 2);
        a.data = snapshot(130, "22345678-1234-1234-1234-123456789abc");
        a.hfp.callback.on3WayCallStateChanged(130, 1, "+79990000001");
        check(sink.count == 3 && sink.last.isVehicleCall() == 0);
        a.data = snapshot(130, "32345678-1234-1234-1234-123456789abc");
        a.hfp.callback.on3WayCallStateChanged(130, 1, "+79990000001");
        check(sink.count == 4);
        b.close();
        System.out.println("ReconnectTest PASS");
    }
}
