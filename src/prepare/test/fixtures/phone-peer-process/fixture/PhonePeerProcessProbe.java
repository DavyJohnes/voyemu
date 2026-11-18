package fixture;

import android.content.Context;
import android.os.Handler;

import com.qinggan.bluetooth.internal.ivt8.Ivt8BTAdapter;
import com.qinggan.bluetooth.profile.BTDevice;
import com.qinggan.bluetooth.profile.Hfp;
import com.qinggan.bluetooth.profile.SimpleHfpCallback;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/** Loaded separately with each adapter; no OEM callback or model object crosses loaders. */
public final class PhonePeerProcessProbe {
    private final Ivt8BTAdapter adapter;
    private final String role;
    private final List<String> calls = new ArrayList<String>();
    private Hfp hfp;

    private PhonePeerProcessProbe(Context context, String role) {
        this.role = role;
        adapter = Ivt8BTAdapter.getInstance(context, null);

        if ("phone".equals(role)) {
            android.os.PeerTestRuntime.install(adapter);
        }

        lateListener();
    }

    public static PhonePeerProcessProbe create(String process) {
        android.os.PeerTestRuntime.enter(process);

        return new PhonePeerProcessProbe(new Context(), process);
    }

    public void lateListener() {
        android.os.PeerTestRuntime.enter(role);
        hfp =
                adapter.getHfp(
                        new SimpleHfpCallback() {
                            public void onReady() {}

                            public void onStateChanged(BTDevice device, int previous, int state) {}

                            public void on3WayCallStateChanged(int state, int id, String number) {
                                if (id != 1
                                        || getCurrentDevice() == null
                                        || !"02:00:00:00:00:01"
                                                .equals(getCurrentDevice().getAddress())) {
                                    throw new AssertionError(
                                            "exact OEM currentDevice must precede callback");
                                }

                                List current = adapter.getHfp(null).getCurrentCalls();

                                if ((state == 131) != current.isEmpty()) {
                                    throw new AssertionError("getter update before callback");
                                }

                                if (!current.isEmpty()) {
                                    Map call = (Map) current.get(0);

                                    if (!number.equals(call.get("call_number"))
                                            || !Integer.valueOf(state)
                                                    .equals(call.get("call_state"))) {
                                        throw new AssertionError(
                                                "callback and actual HFP getter disagree");
                                    }
                                }

                                calls.add(state + ":" + number);
                            }

                            public void onCallStateChanged(int state, String number) {
                                throw new AssertionError("OEM no-op callback");
                            }

                            public void onPhoneMessage(
                                    com.qinggan.bluetooth.bean.BTPhoneMessage message) {}

                            public void onAudioEstablished() {}

                            public void onAudioReleased() {}

                            public void onRingFromPhone(boolean ring) {}
                        });
        Handler.drain();
    }

    public String control(String method, String json) throws Exception {
        android.os.PeerTestRuntime.enter(role);
        String result;

        synchronized (android.os.PeerTestRuntime.MAIN_LOCK) {
            result = adapter.control(method, json);
        }

        Handler.drain();

        return result;
    }

    public void sameProcessControl() throws Exception {
        android.os.PeerTestRuntime.enter("phone");
        Ivt8BTAdapter provider = Ivt8BTAdapter.getInstance(new Context(), null);
        Hfp registered = hfp;

        if (provider != adapter || provider.getHfp(null) != registered) {
            throw new AssertionError("provider/OEM singleton or HFP identity differs");
        }

        int count = calls.size();
        providerControl(
                provider,
                "configure",
                "{\"peer_name\":\"Voyemu Phone\",\"contacts\":[{\"name\":\"Иван"
                        + " Иванов\",\"number\":\"+70000000001\"},{\"name\":\"Анна"
                        + " Петрова\",\"number\":\"+70000000002\"}]}");

        if (Ivt8BTAdapter.getInstance(new Context(), null) != provider
                || provider.getHfp(null) != registered
                || calls.size() != count) {
            throw new AssertionError("configure replaced OEM reference or notified a call");
        }

        providerControl(provider, "connect", "{}");
        providerControl(provider, "incoming", "{\"number\":\"+70000000001\"}");

        if (calls.size() != ++count || !"130:+70000000001".equals(lastCall())) {
            throw new AssertionError("registered same-process incoming callback exactly once");
        }

        Map incoming = (Map) registered.getCurrentCalls().get(0);
        com.qinggan.bluetooth.profile.BluetoothCall incomingModel =
                registered.getCurrentBluetoothCalls().get(0);
        checkModel(incomingModel, 130, "+70000000001", false);
        registered.answerCall();
        Handler.drain();
        Map active = (Map) registered.getCurrentCalls().get(0);
        com.qinggan.bluetooth.profile.BluetoothCall activeModel =
                registered.getCurrentBluetoothCalls().get(0);

        if (calls.size() != ++count
                || active == incoming
                || activeModel == incomingModel
                || !Integer.valueOf(130).equals(incoming.get("call_state"))) {
            throw new AssertionError("fresh active map/model or exactly-once callback");
        }

        checkModel(activeModel, 128, "+70000000001", false);
        registered.hangup();
        Handler.drain();

        if (calls.size() != ++count
                || !registered.getCurrentCalls().isEmpty()
                || !registered.getCurrentBluetoothCalls().isEmpty()) {
            throw new AssertionError("same-process terminated getters/callback");
        }

        verifyOutgoing(provider, registered, incoming, active, count);
    }

    private void verifyOutgoing(
            Ivt8BTAdapter provider, Hfp registered, Map incoming, Map active, int count)
            throws Exception {
        registered.dial("70000000002");
        Handler.drain();
        Map outgoing = (Map) registered.getCurrentCalls().get(0);
        com.qinggan.bluetooth.profile.BluetoothCall outgoingModel =
                registered.getCurrentBluetoothCalls().get(0);

        if (calls.size() != ++count
                || !"129:+70000000002".equals(lastCall())
                || outgoing == active
                || !Integer.valueOf(128).equals(active.get("call_state"))
                || !Integer.valueOf(130).equals(incoming.get("call_state"))) {
            throw new AssertionError("same-process +7 dial or prior snapshot mutation");
        }

        checkModel(outgoingModel, 129, "+70000000002", true);

        if (provider.getHfp(null) != registered
                || Ivt8BTAdapter.getInstance(new Context(), null) != provider) {
            throw new AssertionError("provider mutation replaced retained OEM references");
        }

        registered.hangup();
        Handler.drain();
        providerControl(provider, "disconnect", "{}");
    }

    private void checkModel(
            com.qinggan.bluetooth.profile.BluetoothCall call,
            int state,
            String number,
            boolean outgoing) {
        if (call.device != adapter.getConnectedDevice()
                || call.id != 1
                || call.multi
                || !number.equals(call.number)
                || call.outgoing != outgoing
                || call.state != state
                || call.previous != 131) {
            throw new AssertionError("exact OEM BluetoothCall constructor fields");
        }
    }

    private void providerControl(Ivt8BTAdapter provider, String method, String data)
            throws Exception {
        synchronized (android.os.PeerTestRuntime.MAIN_LOCK) {
            provider.control(method, data);
        }

        Handler.drain();
    }

    public String localSnapshot() throws Exception {
        java.lang.reflect.Method method = adapter.getClass().getDeclaredMethod("snapshot");

        method.setAccessible(true);

        synchronized (android.os.PeerTestRuntime.MAIN_LOCK) {
            return method.invoke(adapter).toString();
        }
    }

    public String callbackCount() {
        Handler.drain();

        return Integer.toString(calls.size());
    }

    public void mirrorMutationsRejected() throws Exception {
        android.os.PeerTestRuntime.enter(role);
        com.qinggan.bluetooth.profile.Pbap pbap = adapter.getPbap(null);

        if (adapter.connect("02:00:00:00:00:01")
                || adapter.disconnect("02:00:00:00:00:01")
                || pbap.connect("02:00:00:00:00:01")
                || pbap.disconnect("02:00:00:00:00:01")
                || pbap.startContactsSync()
                || pbap.startCallLogSyncByRange(0, 200)) {
            throw new AssertionError("mirror mutation accepted");
        }

        try {
            adapter.control("hangup", "{}");
            throw new AssertionError("mirror control accepted");
        } catch (SecurityException expected) {
        }

        Handler.drain();
    }

    public boolean connected() {
        android.os.PeerTestRuntime.enter(role);
        Handler.drain();

        return hfp.isConnected();
    }

    public String lastCall() {
        android.os.PeerTestRuntime.enter(role);
        Handler.drain();

        return calls.isEmpty() ? "" : calls.get(calls.size() - 1);
    }
}
