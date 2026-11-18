package com.qinggan.canbus.service;

import android.os.Bundle;

import com.qinggan.bluetooth.BTManager;
import com.qinggan.bluetooth.profile.BTAdapter;
import com.qinggan.canbus.PhoneInfo;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;

public final class HfpPhoneInfoPrivacyTest {
    private static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    /**
     * Semantic branch oracle only, not an implementation of the OEM masking algorithm. Original
     * gauge ivcmsg_notify 360c4-360d4: location1 -> getPrivateString at366dc, setkzprivatemode1
     * at36710; other location -> raw memcpy360f4, privateMode0 at360fc.
     */
    private static byte[] rawCallerBranch(byte[] frame) {
        if ((frame[7] & 255) == 1) {
            return null;
        } // Private branch: original caller is not exposed raw.

        return Arrays.copyOfRange(frame, 15, 15 + (frame[13] & 255));
    }

    private static byte[] frame(PhoneInfo info) {
        return IpkFrames.phone(
                info.state.name(), info.isVehicleCall(), info.duration, info.name, info.number)[0];
    }

    public static void main(String[] args) {
        // Faithful OEM bean semantics: primitive default0 and setter accepts arbitrary int.
        PhoneInfo bean = new PhoneInfo();

        check(bean.isVehicleCall() == 0);
        bean.setVehicleCall(7);
        check(bean.isVehicleCall() == 7);
        BTAdapter adapter = BTManager.INSTANCE.adapter;
        Bundle snapshot = new Bundle();
        snapshot.put("schema", 1);
        snapshot.put("state", 130);
        snapshot.put("number", "+70000000001");
        snapshot.put("name", "Иван Иванов");
        snapshot.put("active_started_elapsed_ms", 0L);
        snapshot.put("connected", true);
        snapshot.put("incarnation", "12345678-1234-1234-1234-123456789abc");
        adapter.data = snapshot;
        CanBusStubService sink = new CanBusStubService();
        HfpPhoneInfoBridge bridge =
                new HfpPhoneInfoBridge(
                        new android.content.Context(), sink, new PhoneDeliveryDiagnostics());
        bridge.start();
        adapter.hfp.callback.on3WayCallStateChanged(130, 1, "+70000000001");
        check(sink.count == 1);
        byte[] plain = frame(sink.last);
        byte[] expected = "Иван Иванов".getBytes(StandardCharsets.UTF_8);
        // Fails with the old bridge: unconditional setVehicleCall1 selects OEM privacy branch.
        check(Arrays.equals(rawCallerBranch(plain), expected));
        check(plain[6] == 2 && plain[7] == 2 && plain[12] == 1);
        sink.last.setVehicleCall(1);
        byte[] privateFrame = frame(sink.last);
        check(privateFrame[7] == 1 && rawCallerBranch(privateFrame) == null);
        byte[] same = plain.clone();
        same[7] = 1;
        check(
                Arrays.equals(
                        same,
                        privateFrame)); // State, caller bytes and protocol are otherwise unchanged.
        bridge.close();
        System.out.println("HfpPhoneInfoPrivacyTest PASS");
    }
}
