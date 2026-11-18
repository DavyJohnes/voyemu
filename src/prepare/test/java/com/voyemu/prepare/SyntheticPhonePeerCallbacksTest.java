package com.voyemu.prepare;

import android.content.Context;
import android.os.Handler;

import com.qinggan.bluetooth.bean.BTCallLogItem;
import com.qinggan.bluetooth.bean.BTContactItem;
import com.qinggan.bluetooth.bean.BTPhoneMessage;
import com.qinggan.bluetooth.internal.ivt8.Ivt8BTAdapter;
import com.qinggan.bluetooth.profile.BTDevice;
import com.qinggan.bluetooth.profile.Hfp;
import com.qinggan.bluetooth.profile.Pbap;
import com.qinggan.bluetooth.profile.SimpleHfpCallback;
import com.qinggan.bluetooth.profile.SimplePbapCallback;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/** Host callback contract test; fixture Handler queues work instead of using a guest. */
public final class SyntheticPhonePeerCallbacksTest {
    private static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    public static void main(String[] args) throws Exception {
        Context context = new Context();
        Path expected = Path.of(args[0]).toRealPath();
        Path files = context.getFilesDir().toPath().toRealPath();

        check(files.getParent().equals(expected.resolve("files")));
        check(
                Path.of(System.getProperty("java.io.tmpdir"))
                        .toRealPath()
                        .equals(expected.resolve("tmp")));
        check(Path.of("").toRealPath().equals(expected));
        Ivt8BTAdapter adapter = Ivt8BTAdapter.getInstance(new Context(), null);
        final List<Integer> callStates = new ArrayList<>();
        final List<String> contacts = new ArrayList<>(), numbers = new ArrayList<>();
        final List<Integer> sync = new ArrayList<>();
        final int[] ready = {0}, count = {0};
        Hfp hfp =
                adapter.getHfp(
                        new SimpleHfpCallback() {
                            @Override
                            public void onReady() {
                                ready[0]++;
                            }

                            @Override
                            public void onStateChanged(BTDevice d, int previous, int state) {
                                check(d.getAddress().equals("02:00:00:00:00:01"));
                            }

                            // Checks that the same device instance is reported.
                            @SuppressWarnings("ReferenceEquality")
                            @Override
                            public void on3WayCallStateChanged(int state, int id, String number) {
                                check(id == 1);
                                check(
                                        getCurrentDevice() == adapter.getConnectedDevice()
                                                || (state == 131 && getCurrentDevice() != null));
                                check(getCurrentDevice().getAddress().equals("02:00:00:00:00:01"));
                                callStates.add(state);
                                List calls = adapter.getHfp(null).getCurrentCalls();
                                check((state == 131) == calls.isEmpty());

                                if (!calls.isEmpty()) {
                                    Map call = (Map) calls.get(0);
                                    check(call.get("call_number").equals(number));
                                    check(call.get("call_state").equals(state));
                                }
                            }

                            public void onCallStateChanged(int s, String n) {
                                throw new AssertionError("deprecated UI no-op used");
                            }

                            @Override
                            public void onPhoneMessage(BTPhoneMessage message) {
                                throw new AssertionError("no-op used");
                            }

                            @Override
                            public void onAudioEstablished() {
                                throw new AssertionError("synthetic audio");
                            }

                            @Override
                            public void onAudioReleased() {}

                            @Override
                            public void onRingFromPhone(boolean ring) {}
                        });
        Pbap pbap =
                adapter.getPbap(
                        new SimplePbapCallback() {
                            @Override
                            public void onReady() {
                                ready[0]++;
                            }

                            @Override
                            public void onStateChanged(BTDevice d, int previous, int state) {}

                            @Override
                            public void onContactItemCountUpdated(int progress, int total) {
                                check(progress == 0);
                                count[0] = total;
                            }

                            @Override
                            public void onContactItemFetched(BTContactItem item) {
                                check(
                                        getSyncDevice() != null
                                                && getSyncDevice()
                                                        .getAddress()
                                                        .equals("02:00:00:00:00:01"));
                                contacts.add(item.name);
                                numbers.add(item.numbers.get(0).number);
                                check(item.numbers.get(0).type == 160);
                            }

                            @Override
                            public void onSyncStateChanged(int before, int state) {
                                sync.add(state);

                                if (state == 97 && contacts.isEmpty()) {
                                    check(adapter.getPbap(null).startContactsSync());
                                }
                            }

                            @Override
                            public void onCallLogItemCountUpdated(int progress, int total) {}

                            @Override
                            public void onCallLogItemFetched(BTCallLogItem item) {}
                        });

        check(!pbap.startContactsSync());
        Handler.drain();
        check(ready[0] == 2 && adapter.isEnabled());
        check(adapter.connect("02:00:00:00:00:01"));
        Handler.drain();
        check(
                hfp.isConnected()
                        && pbap.isConnected()
                        && hfp.getConnectedDevices().size() == 1
                        && hfp.getNetworkStatus() == 1);
        check(!pbap.startCallLogSyncByRange(-1, 200));
        check(pbap.startCallLogSyncByRange(0, 200));
        Handler.drain();
        check(
                count[0] == 2
                        && contacts.get(0).equals("Иван Иванов")
                        && contacts.get(1).equals("Анна Петрова"));
        check(
                numbers.get(0).equals("+70000000001")
                        && sync.equals(java.util.Arrays.asList(96, 97, 96, 97)));
        checkDialing(hfp, callStates);
        checkReconnect(adapter, hfp, pbap, contacts);
        System.out.println("SyntheticPhonePeerCallbacksTest PASS");
    }

    private static void checkDialing(Hfp hfp, List<Integer> callStates) {
        hfp.dial("70000000001", "02:00:00:00:00:01");
        Handler.drain();
        check(callStates.equals(java.util.Arrays.asList(129)));
        hfp.hangup(1);
        Handler.drain();
        check(callStates.equals(java.util.Arrays.asList(129, 131)));
        hfp.dial("7999");
        Handler.drain();
        check(hfp.getCurrentCalls().isEmpty() && callStates.size() == 2);
        check(hfp.getPhoneNumber().isEmpty());
        int beforeDial = callStates.size();

        for (String invalid :
                new String[] {
                    null,
                    "",
                    "7999",
                    "70000000003",
                    "17000000001",
                    "+17000000001",
                    "87000000001",
                    "7 0000000001",
                    "70000000001 ",
                    "70000000001x",
                    "++70000000001"
                }) {
            hfp.dial(invalid);
            Handler.drain();
            check(hfp.getCurrentCalls().isEmpty() && callStates.size() == beforeDial);
        }

        hfp.dial("70000000002", "02:00:00:00:00:02");
        Handler.drain();
        check(callStates.size() == beforeDial);
        hfp.dial("70000000002");
        Handler.drain();
        check(
                hfp.getPhoneNumber().isEmpty()
                        && ((Map) hfp.getCurrentCalls().get(0))
                                .get("call_number")
                                .equals("+70000000002")
                        && callStates.size() == beforeDial + 1);
        hfp.hangup(2);
        Handler.drain();
        check(!hfp.getCurrentCalls().isEmpty());
        hfp.hangup(1);
        Handler.drain();
        check(hfp.getCurrentCalls().isEmpty());
        hfp.dial("+70000000001");
        Handler.drain();
        check(
                hfp.getPhoneNumber().isEmpty()
                        && ((Map) hfp.getCurrentCalls().get(0))
                                .get("call_number")
                                .equals("+70000000001"));
        hfp.hangup();
        Handler.drain();
        hfp.dial("+70000000003");
        Handler.drain();
        check(
                hfp.getPhoneNumber().isEmpty()
                        && ((Map) hfp.getCurrentCalls().get(0))
                                .get("call_number")
                                .equals("+70000000003"));
        hfp.hangup();
        Handler.drain();
    }

    private static void checkReconnect(
            Ivt8BTAdapter adapter, Hfp hfp, Pbap pbap, List<String> contacts) {
        check(pbap.disconnect("02:00:00:00:00:01"));
        Handler.drain();
        check(!pbap.isConnected() && hfp.isConnected());
        check(pbap.connect("02:00:00:00:00:01"));
        Handler.drain();
        check(pbap.startContactsSync());
        Handler.drain();
        check(contacts.size() == 4);
        check(adapter.disconnect("02:00:00:00:00:01"));
        Handler.drain();
        check(!hfp.isConnected() && !pbap.startContactsSync());
        check(!adapter.disable() && adapter.isEnabled());
    }
}
