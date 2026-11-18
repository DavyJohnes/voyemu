package com.qinggan.bluetooth.internal.ivt8;

import android.content.Context;
import android.os.Looper;

import com.qinggan.bluetooth.BTConst;
import com.qinggan.bluetooth.BTDeviceManager;
import com.qinggan.bluetooth.bean.BTContactItem;
import com.qinggan.bluetooth.bean.BTPhoneNumber;
import com.qinggan.bluetooth.profile.A2dp;
import com.qinggan.bluetooth.profile.BTAdapter;
import com.qinggan.bluetooth.profile.BTDevice;
import com.qinggan.bluetooth.profile.BluetoothCall;
import com.qinggan.bluetooth.profile.Hfp;
import com.qinggan.bluetooth.profile.IA2dpCallback;
import com.qinggan.bluetooth.profile.IBTCallback;
import com.qinggan.bluetooth.profile.IHfpCallback;
import com.qinggan.bluetooth.profile.IPbapCallback;
import com.qinggan.bluetooth.profile.IRfcommCallback;
import com.qinggan.bluetooth.profile.ISppCallback;
import com.qinggan.bluetooth.profile.Pbap;
import com.qinggan.bluetooth.profile.Rfcomm;
import com.qinggan.bluetooth.profile.SimpleHfpCallback;
import com.qinggan.bluetooth.profile.SimplePbapCallback;
import com.qinggan.bluetooth.profile.Spp;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;

/** Synthetic OEM peer: existing phone importer and callbacks, no physical radio. */
public final class Ivt8BTAdapter extends BTAdapter {

    private final SyntheticPhonePeer peer = new SyntheticPhonePeer();
    private final PeerDevice device;
    private final PeerHfp hfp;
    private final A2dp inactiveA2dp;
    private final PeerPbap pbap;
    private static Ivt8BTAdapter sInstance;
    private final boolean authority;
    private final PeerSubscription subscription;
    private final PeerConfiguration configuration;
    private final String incarnation = java.util.UUID.randomUUID().toString();
    private String remoteIncarnation;
    private long remoteRevision = -1;
    private volatile long activeStartedElapsed;

    private Ivt8BTAdapter(Context context, BTDeviceManager manager) {
        super(context, manager);
        device = new PeerDevice(context);
        hfp = new PeerHfp(context, manager);
        inactiveA2dp = new InactiveA2dp(context, manager);
        pbap = new PeerPbap(context, manager);
        mA2dp = inactiveA2dp;
        mHfp = hfp;
        mPbap = pbap;
        mIsReady = true;
        authority = PeerSubscription.authoritative(context);
        configuration = authority ? new PeerConfiguration(context.getApplicationContext()) : null;

        if (configuration != null) {
            configuration.restore(peer);
        }

        subscription =
                new PeerSubscription(
                        context,
                        mHandler,
                        new PeerSubscription.State() {
                            public String snapshot() throws Exception {
                                return Ivt8BTAdapter.this.wireSnapshot().toString();
                            }

                            public void accept(JSONObject value) throws Exception {
                                acceptRemote(value);
                            }

                            public void lost() {
                                boolean before = peer.connected();

                                peer.unavailable();
                                remoteIncarnation = null;
                                remoteRevision = -1;
                                activeStartedElapsed = 0;

                                if (before) {
                                    hfp.remoteCallChanged();
                                    hfp.remoteConnectionChanged(16, 19);
                                }
                            }
                        },
                        authority);

        if (!authority
                && PeerSubscription.process(
                        context,
                        android.os.Process.myUid(),
                        android.os.Process.myPid(),
                        "com.qinggan.app.setting")) {
            subscription.listen(true);
        }
    }

    public static synchronized Ivt8BTAdapter getInstance(Context context, BTDeviceManager manager) {
        if (sInstance == null) {
            sInstance = new Ivt8BTAdapter(context, manager);
        }

        return sInstance;
    }

    @Override
    public boolean cancelDiscovery() {
        return false;
    }

    @Override
    public boolean connect(BTDevice device) {
        if (!isPeer(device)) {
            return false;
        }

        if (!authority) {
            return remoteIncarnation != null
                    && peer.state() == SyntheticPhonePeer.TERMINATED
                    && !peer.connected()
                    && subscription.settingsCommand("connect", device.getAddress());
        }

        mHandler.post(() -> {
            if (!peer.connected()) {
                connectPeer();
            }
        });

        return true;
    }

    @Override
    public boolean createBond(BTDevice device) {
        return false;
    }

    @Override
    public boolean disable() {
        return false;
    }

    @Override
    public boolean disconnect(BTDevice device) {
        if (!isPeer(device)) {
            return false;
        }

        if (!authority) {
            return remoteIncarnation != null
                    && peer.state() == SyntheticPhonePeer.TERMINATED
                    && peer.connected()
                    && subscription.settingsCommand("disconnect", device.getAddress());
        }

        mHandler.post(() -> {
            if (peer.connected()) {
                disconnectPeer();
            }
        });

        return true;
    }

    @Override
    public boolean enable() {
        return true;
    }

    @Override
    public boolean enableAutoBond() {
        return false;
    }

    @Override
    public A2dp getA2dp(IA2dpCallback callback) {
        inactiveA2dp.registerListener(callback);

        return inactiveA2dp;
    }

    @Override
    public String getAddress() {
        return "00:00:00:00:00:00";
    }

    @Override
    public int getBondState(BTDevice device) {
        return isPeer(device) ? 64 : BTConst.BOND_NONE;
    }

    @Override
    public BTDevice getConnectedDevice() {
        return peer.connected() ? device : null;
    }

    @Override
    public int getConnectionState() {
        return peer.connected() ? 16 : 19;
    }

    @Override
    public int getConnectionState(BTDevice device) {
        return isPeer(device) ? getConnectionState() : 19;
    }

    @Override
    public String getName() {
        return "Voyemu-Emulator";
    }

    @Override
    public Pbap getPbap(IPbapCallback callback) {
        pbap.registerListener(callback);

        return pbap;
    }

    @Override
    public Rfcomm getRfcommForClient(
            Context context, IRfcommCallback callback, BTDevice device, int value, boolean flag) {
        return null;
    }

    @Override
    public Rfcomm getRfcommForClient(
            Context context, IRfcommCallback callback, BTDevice device, UUID uuid, boolean flag) {
        return null;
    }

    @Override
    public Rfcomm getRfcommForServer(
            Context context, IRfcommCallback callback, int value, boolean flag) {
        return null;
    }

    @Override
    public Rfcomm getRfcommForServer(
            Context context, IRfcommCallback callback, String text, UUID uuid, boolean flag) {
        return null;
    }

    @Override
    public int getScanMode() {
        return BTConst.SCAN_MODE_NONE;
    }

    @Override
    public Spp getSpp(ISppCallback callback) {
        return null;
    }

    @Override
    public int getState() {
        return 1;
    }

    @Override
    public boolean isDiscovering() {
        return false;
    }

    @Override
    public boolean isEnabled() {
        return true;
    }

    @Override
    public boolean removeBond(BTDevice device) {
        return false;
    }

    @Override
    public void requestBondedDevices() {
        mHandler.post(() -> {
            Set<BTDevice> result = new HashSet<>();
            result.add(device);

            for (IBTCallback listener : new ArrayList<>(mListeners)) {
                listener.onRequestBondedDevices(result);
            }
        });
    }

    @Override
    public void setDiscoverableTimeout(int value) {}

    @Override
    public void setName(String text) {}

    @Override
    public void setScanMode(int value) {}

    @Override
    public boolean setScanMode(int value, int value2) {
        return false;
    }

    @Override
    public boolean startDiscovery() {
        return false;
    }

    @Override
    public Hfp getHfp(IHfpCallback callback) {
        hfp.registerListener(callback);

        return hfp;
    }

    @Override
    public BTDevice getRemoteDevice(String address) {
        return SyntheticPhonePeer.ADDRESS.equals(address) ? device : null;
    }

    @Override
    public List<BTDevice> getBondedDevices() {
        return java.util.Collections.<BTDevice>singletonList(device);
    }

    @Override
    public boolean connect(String address) {
        return SyntheticPhonePeer.ADDRESS.equals(address) && connect(device);
    }

    @Override
    public boolean disconnect(String address) {
        return SyntheticPhonePeer.ADDRESS.equals(address) && disconnect(device);
    }

    @Override
    public int getHfpState(String address) {
        return SyntheticPhonePeer.ADDRESS.equals(address) ? getConnectionState() : 19;
    }

    private boolean isPeer(BTDevice value) {
        return value != null && SyntheticPhonePeer.ADDRESS.equals(value.getAddress());
    }

    /**
     * Binder callers wait for the handler transaction; queued cancelled work never mutates state.
     */
    public String control(String method, String json) throws Exception {
        if (!authority) {
            throw new SecurityException("PHONE_PEER_READ_ONLY");
        }

        if (method == null || json == null || json.length() > 4096) {
            throw new IllegalArgumentException("PHONE_REQUEST");
        }

        FutureTask<String> work =
                new FutureTask<>(() -> controlOnHandler(method, new JSONObject(json)));

        if (Looper.myLooper() == mHandler.getLooper()) {
            work.run();
        } else if (!mHandler.post(work)) {
            throw new IllegalStateException("PHONE_HANDLER_STOPPED");
        }

        try {
            return work.get(2, TimeUnit.SECONDS);
        } catch (java.util.concurrent.TimeoutException timeout) {
            work.cancel(false);
            throw new IllegalStateException("PHONE_TIMEOUT_OUTCOME_UNKNOWN", timeout);
        } catch (InterruptedException interrupted) {
            work.cancel(false);
            Thread.currentThread().interrupt();
            throw interrupted;
        } catch (java.util.concurrent.ExecutionException failure) {
            if (failure.getCause() instanceof Exception) {
                throw (Exception) failure.getCause();
            }

            throw failure;
        }
    }

    private String controlOnHandler(String method, JSONObject args) throws Exception {
        if ("configure".equals(method)) {
            keys(args, "contacts", "peer_name");
            JSONArray items = args.getJSONArray("contacts");

            if (items.length() != 2) {
                throw new IllegalArgumentException("PHONE_TWO_CONTACTS");
            }

            List<SyntheticPhonePeer.Contact> contacts = new ArrayList<>();

            for (int i = 0; i < 2; i++) {
                JSONObject item = items.getJSONObject(i);
                keys(item, "name", "number");
                contacts.add(
                        new SyntheticPhonePeer.Contact(
                                string(item, "name"), string(item, "number")));
            }

            configuration.configure(
                    peer,
                    args.has("peer_name") ? string(args, "peer_name") : "Voyemu Phone",
                    contacts);
        } else if ("incoming".equals(method)) {
            keys(args, "number");
            peer.incoming(string(args, "number"));
            hfp.callChanged();
        } else {
            keys(args);

            if ("snapshot".equals(method)) {
                // checkstyle-allow EmptyBlock: snapshot only reads the peer state
            } else if ("connect".equals(method)) {
                connectPeer();
            } else if ("disconnect".equals(method)) {
                disconnectPeer();
            } else if ("answer".equals(method)) {
                peer.answer();
                hfp.callChanged();
            } else if ("hangup".equals(method)) {
                peer.hangup();
                hfp.callChanged();
            } else {
                throw new IllegalArgumentException("PHONE_METHOD");
            }
        }

        if (!"snapshot".equals(method)) {
            subscription.publish();
        }

        return snapshot().toString();
    }

    private static String string(JSONObject object, String name) throws Exception {
        Object value = object.get(name);

        if (!(value instanceof String)) {
            throw new IllegalArgumentException("PHONE_STRING_" + name);
        }

        return (String) value;
    }

    private static void keys(JSONObject object, String... allowed) {
        java.util.Iterator<String> it = object.keys();

        while (it.hasNext()) {
            String key = it.next();
            boolean found = false;

            for (String value : allowed) {
                if (value.equals(key)) {
                    found = true;
                }
            }

            if (!found) {
                throw new IllegalArgumentException("PHONE_FIELD_" + key);
            }
        }
    }

    private JSONObject snapshot() throws Exception {
        JSONObject diagnostics = subscription.diagnostics();

        diagnostics
                .put("hfp_listeners", hfp.callbacks.size())
                .put("hfp_last_state", hfp.lastDeliveredState);

        return wireSnapshot().put("diagnostics", diagnostics);
    }

    private JSONObject wireSnapshot() throws Exception {
        JSONObject result = new JSONObject();

        result.put("ok", true)
                .put("incarnation", incarnation)
                .put("sequence", peer.sequence())
                .put("connected", peer.connected());
        result.put("pbap_connected", pbap.connected).put("sync_state", pbap.syncState);
        result.put("peer_name", peer.name()).put("peer_address", SyntheticPhonePeer.ADDRESS);
        result.put(
                "call",
                new JSONObject()
                        .put("state", peer.state())
                        .put("id", 1)
                        .put("number", peer.number())
                        .put("outgoing", peer.outgoingCall())
                        .put("active_started_elapsed_ms", activeStartedElapsed));
        JSONArray contacts = new JSONArray();

        for (SyntheticPhonePeer.Contact c : peer.contacts()) {
            contacts.put(new JSONObject().put("name", c.name).put("number", c.number));
        }

        result.put("contacts", contacts)
                .put("outgoing", new JSONArray(peer.outgoing()))
                .put("events", new JSONArray(peer.journal()));

        return result;
    }

    /**
     * Called only by authenticated PhonePeerProvider while holding the host/Settings arbitration
     * lock.
     */
    public boolean settingsPeer(
            String operation, String address, android.os.IBinder callback, int uid, int pid)
            throws Exception {
        if (!authority
                || Looper.myLooper() != mHandler.getLooper()
                || !SyntheticPhonePeer.ADDRESS.equals(address)) {
            return false;
        }

        return subscription.settingsMutation(
                callback,
                uid,
                pid,
                () -> {
                    if (peer.state() != SyntheticPhonePeer.TERMINATED) {
                        return false;
                    }

                    if ("connect".equals(operation) && !peer.connected()) {
                        connectPeer();

                        return true;
                    }

                    if ("disconnect".equals(operation) && peer.connected()) {
                        disconnectPeer();

                        return true;
                    }

                    return false;
                });
    }

    public android.os.Bundle subscribePeer(android.os.IBinder callback, int uid, int pid)
            throws Exception {
        return subscription.subscribe(callback, uid, pid);
    }

    public void unsubscribePeer(android.os.IBinder callback, int uid, int pid) {
        subscription.unsubscribe(callback, uid, pid);
    }

    private void acceptRemote(JSONObject value) throws Exception {
        String epoch = remoteEpoch(value);
        long next = remoteSequence(value);

        if (epoch.equals(remoteIncarnation) && next <= remoteRevision) {
            return;
        }

        if (remoteIncarnation != null && !epoch.equals(remoteIncarnation)) {
            throw new IllegalArgumentException("PHONE_PEER_EPOCH");
        }

        Object connected = value.get("connected"), pbapConnected = value.get("pbap_connected");

        if (!(connected instanceof Boolean) || !(pbapConnected instanceof Boolean)) {
            throw new IllegalArgumentException("PHONE_PEER_BOOLEAN");
        }

        JSONObject call = value.getJSONObject("call");
        int callState = remoteCallState(call);
        long callStart = integer(call, "active_started_elapsed_ms");
        long sync = integer(value, "sync_state");

        if (sync < 95 || sync > 98) {
            throw new IllegalArgumentException("PHONE_PEER_SYNC");
        }

        requireRemoteJournal(value.getJSONArray("outgoing"), value.getJSONArray("events"));
        List<SyntheticPhonePeer.Contact> contacts = remoteContacts(value.getJSONArray("contacts"));
        boolean before = peer.connected();
        int previousState = peer.state();
        String previousNumber = peer.number();

        if ((Boolean) pbapConnected && !(Boolean) connected) {
            throw new IllegalArgumentException("PHONE_PEER_CONNECTION");
        }

        peer.mirror(
                string(value, "peer_name"),
                contacts,
                (Boolean) connected,
                callState,
                string(call, "number"),
                (Boolean) call.get("outgoing"),
                next);
        pbap.connected = (Boolean) pbapConnected;
        pbap.syncState = (int) sync;
        activeStartedElapsed = callStart;
        remoteIncarnation = epoch;
        remoteRevision = next;
        remoteChanged(before, previousState, previousNumber);
    }

    /** Checks the snapshot envelope and returns its incarnation. */
    private String remoteEpoch(JSONObject value) throws Exception {
        keys(
                value,
                "ok",
                "incarnation",
                "sequence",
                "connected",
                "pbap_connected",
                "sync_state",
                "peer_name",
                "peer_address",
                "call",
                "contacts",
                "outgoing",
                "events");

        if (authority
                || value.length() != 12
                || !Boolean.TRUE.equals(value.get("ok"))
                || !SyntheticPhonePeer.ADDRESS.equals(value.get("peer_address"))) {
            throw new IllegalArgumentException("PHONE_PEER_SNAPSHOT");
        }

        String epoch = string(value, "incarnation");

        if (!epoch.matches("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}")) {
            throw new IllegalArgumentException("PHONE_PEER_INCARNATION");
        }

        return epoch;
    }

    private static long remoteSequence(JSONObject value) throws Exception {
        Object revision = value.get("sequence");

        if (!(revision instanceof Integer || revision instanceof Long)) {
            throw new IllegalArgumentException("PHONE_PEER_REVISION");
        }

        long next = ((Number) revision).longValue();

        if (next < 0) {
            throw new IllegalArgumentException("PHONE_PEER_REVISION");
        }

        return next;
    }

    private static int remoteCallState(JSONObject call) throws Exception {
        keys(call, "id", "state", "number", "outgoing", "active_started_elapsed_ms");

        if (call.length() != 5
                || integer(call, "id") != 1
                || !(call.get("outgoing") instanceof Boolean)) {
            throw new IllegalArgumentException("PHONE_PEER_CALL");
        }

        long callStart = integer(call, "active_started_elapsed_ms"),
                rawState = integer(call, "state");

        if (rawState != 128 && rawState != 129 && rawState != 130 && rawState != 131) {
            throw new IllegalArgumentException("PHONE_PEER_CALL_STATE");
        }

        int callState = (int) rawState;

        if (callStart < 0 || (callState != 128 && callStart != 0)) {
            throw new IllegalArgumentException("PHONE_PEER_CALL_START");
        }

        return callState;
    }

    private static void requireRemoteJournal(JSONArray outgoing, JSONArray events)
            throws Exception {
        if (outgoing.length() > 32 || events.length() > 64) {
            throw new IllegalArgumentException("PHONE_PEER_JOURNAL");
        }

        for (int i = 0; i < outgoing.length(); i++) {
            Object item = outgoing.get(i);

            if (!(item instanceof String)) {
                throw new IllegalArgumentException("PHONE_PEER_OUTGOING");
            }

            SyntheticPhonePeer.validateNumber((String) item);
        }

        for (int i = 0; i < events.length(); i++) {
            Object item = events.get(i);

            if (!(item instanceof String)
                    || !((String) item)
                            .matches(
                                    "[0-9]{1,19}:(configure|connect|disconnect|incoming|dial|answer|hangup|pbap_contacts)")) {
                throw new IllegalArgumentException("PHONE_PEER_EVENT");
            }
        }
    }

    private static List<SyntheticPhonePeer.Contact> remoteContacts(JSONArray array)
            throws Exception {
        if (array.length() != 2) {
            throw new IllegalArgumentException("PHONE_PEER_CONTACTS");
        }

        List<SyntheticPhonePeer.Contact> contacts = new ArrayList<>();

        for (int i = 0; i < 2; i++) {
            JSONObject contact = array.getJSONObject(i);
            keys(contact, "name", "number");

            if (contact.length() != 2) {
                throw new IllegalArgumentException("PHONE_PEER_CONTACT");
            }

            contacts.add(
                    new SyntheticPhonePeer.Contact(
                            string(contact, "name"), string(contact, "number")));
        }

        return contacts;
    }

    private void remoteChanged(boolean before, int previousState, String previousNumber) {
        if (before != peer.connected()) {
            hfp.remoteConnectionChanged(before ? 16 : 19, peer.connected() ? 16 : 19);

            for (IBTCallback callback : new ArrayList<>(mListeners)) {
                try {
                    callback.onConnectionStateChanged(
                            device, before ? 16 : 19, peer.connected() ? 16 : 19);
                } catch (RuntimeException error) {
                    // checkstyle-allow IllegalCatch: a failing OEM listener is a recorded consumer
                    // failure
                    subscription.consumerFailure("IBT_CONNECTION", error);
                }
            }
        }

        if (previousState != peer.state() || !previousNumber.equals(peer.number())) {
            hfp.remoteCallChanged();
        }
    }

    private static long integer(JSONObject object, String key) throws Exception {
        Object value = object.get(key);

        if (!(value instanceof Integer || value instanceof Long)) {
            throw new IllegalArgumentException("PHONE_PEER_INTEGER");
        }

        return ((Number) value).longValue();
    }

    public android.os.Bundle phoneInfoSnapshot() {
        android.os.Bundle value = new android.os.Bundle();

        value.putInt("schema", 1);
        value.putInt("state", peer.state());
        value.putString("number", peer.number());
        String name = "";

        for (SyntheticPhonePeer.Contact contact : peer.contacts()) {
            if (contact.number.equals(peer.number())) {
                name = contact.name;
            }
        }

        value.putString("name", name);
        value.putLong("active_started_elapsed_ms", activeStartedElapsed);
        value.putBoolean("connected", peer.connected());
        value.putString("incarnation", authority ? incarnation : remoteIncarnation);

        return value;
    }

    private void connectPeer() {
        peer.connect();

        for (IBTCallback callback : new ArrayList<>(mListeners)) {
            callback.onConnectionStateChanged(device, 19, 16);
        }

        hfp.connectionChanged(19, 16);
        pbap.setConnected(true);
        subscription.publish();
    }

    private void disconnectPeer() {
        peer.disconnect();
        hfp.callChanged();
        pbap.setConnected(false);
        hfp.connectionChanged(16, 19);

        for (IBTCallback callback : new ArrayList<>(mListeners)) {
            callback.onConnectionStateChanged(device, 16, 19);
        }

        subscription.publish();
    }

    private final class PeerDevice extends BTDevice {
        PeerDevice(Context context) {
            super(context);
        }

        public String getAddress() {
            return SyntheticPhonePeer.ADDRESS;
        }

        public int getBTClass() {
            return 113;
        }

        public int getBTClassForAdapterHeadset() {
            return 113;
        }

        public String getName() {
            return peer.name();
        }

        public android.bluetooth.BluetoothDevice getRaw() {
            return null;
        }

        protected void refreshAttr(BTDevice ignored) {}

        protected void refreshName(BTDevice ignored) {}
    }

    /** OEM callers require a profile object even when no media transport is available. */
    private static final class InactiveA2dp extends A2dp {
        InactiveA2dp(Context context, BTDeviceManager manager) {
            super(context, manager);
        }

        public boolean connect(BTDevice value) {
            return false;
        }

        public boolean connect(String address) {
            return false;
        }

        public boolean disconnect(BTDevice value) {
            return false;
        }

        public boolean disconnect(String address) {
            return false;
        }

        public int getConnectionState() {
            return 19;
        }

        public int getConnectionState(BTDevice value) {
            if (value == null) {
                throw new IllegalArgumentException("device can not be null!!");
            }

            return 19;
        }

        public boolean isConnected() {
            return false;
        }

        public boolean isProfileReady() {
            return false;
        }

        public boolean isPlaying() {
            return false;
        }

        public boolean backward() {
            return false;
        }

        public boolean forward() {
            return false;
        }

        public boolean pause() {
            return false;
        }

        public boolean play() {
            return false;
        }

        public boolean stop() {
            return false;
        }

        public boolean startFastForward() {
            return false;
        }

        public boolean stopFastForward() {
            return false;
        }

        public boolean startRewind() {
            return false;
        }

        public boolean stopRewind() {
            return false;
        }

        public boolean volumnDown() {
            return false;
        }

        public boolean volumnUp() {
            return false;
        }

        public void changeFolderPath(int direction, byte[] path) {}

        public void mediaBrowsing() {}

        public void nowPlayingReq() {}

        public void playMediaItem(byte[] item) {}
    }

    private final class PeerHfp extends Hfp {
        private final List<IHfpCallback> callbacks = new ArrayList<>();
        private int lastDeliveredState = -1;

        PeerHfp(Context context, BTDeviceManager manager) {
            super(context, manager);
        }

        public void registerListener(IHfpCallback callback) {
            if (callback == null || callbacks.contains(callback)) {
                return;
            }

            callbacks.add(callback);

            if (subscription != null) {
                subscription.note(
                        phoneCallback(callback) ? "HFP_REGISTER_PHONE" : "HFP_REGISTER_OTHER");
            }

            mHandler.post(() -> {
                if (callbacks.contains(callback)) {
                    if (authority) {
                        callback.onReady();

                        if (peer.connected()) {
                            callback.onStateChanged(device, 19, 16);
                            notifyCall(callback);
                        }
                    } else {
                        try {
                            callback.onReady();
                        } catch (RuntimeException error) {
                            // checkstyle-allow IllegalCatch: a failing OEM listener is a recorded
                            // consumer failure
                            subscription.consumerFailure("HFP_READY", error);
                        }

                        if (peer.connected()) {
                            remoteConnection(callback, 19, 16);
                            remoteCall(callback);
                        }
                    }
                }
            });

            if (subscription != null) {
                subscription.listen(true);
            }
        }

        public void unregisterListener(IHfpCallback callback) {
            callbacks.remove(callback);

            if (callbacks.isEmpty() && subscription != null) {
                subscription.listen(false);
            }
        }

        void connectionChanged(int before, int after) {
            for (IHfpCallback c : new ArrayList<>(callbacks)) {
                c.onStateChanged(device, before, after);
            }
        }

        void remoteConnectionChanged(int before, int after) {
            for (IHfpCallback callback : new ArrayList<>(callbacks)) {
                remoteConnection(callback, before, after);
            }
        }

        private void remoteConnection(IHfpCallback callback, int before, int after) {
            try {
                callback.onStateChanged(device, before, after);
            } catch (RuntimeException error) {
                // checkstyle-allow IllegalCatch: a failing OEM listener is a recorded consumer
                // failure
                subscription.consumerFailure("HFP_CONNECTION", error);
            }
        }

        void remoteCallChanged() {
            for (IHfpCallback callback : new ArrayList<>(callbacks)) {
                remoteCall(callback);
            }
        }

        private void remoteCall(IHfpCallback callback) {
            bindCallDevice(callback);

            try {
                callback.on3WayCallStateChanged(peer.state(), 1, peer.number());
            } catch (RuntimeException error) {
                // checkstyle-allow IllegalCatch: a failing OEM listener is a recorded consumer
                // failure
                subscription.consumerFailure("HFP_CALL", error);
            }
        }

        void callChanged() {
            if (authority) {
                if (peer.state() == SyntheticPhonePeer.ACTIVE && activeStartedElapsed == 0) {
                    activeStartedElapsed = android.os.SystemClock.elapsedRealtime();
                } else if (peer.state() != SyntheticPhonePeer.ACTIVE) {
                    activeStartedElapsed = 0;
                }
            }

            for (IHfpCallback c : new ArrayList<>(callbacks)) {
                notifyCall(c);
            }

            if (subscription != null) {
                subscription.publish();
            }
        }

        boolean phoneCallback(IHfpCallback callback) {
            return "com.qinggan.bluetoothphone.logic.manager.HeadSetProfileManager$HeadSetProfile$2"
                    .equals(callback.getClass().getName());
        }

        void notifyCall(IHfpCallback callback) {
            bindCallDevice(callback);
            callback.on3WayCallStateChanged(peer.state(), 1, peer.number());
        }

        private void bindCallDevice(IHfpCallback callback) {
            lastDeliveredState = peer.state();

            if (subscription != null) {
                subscription.note(
                        phoneCallback(callback) ? "HFP_DELIVER_PHONE" : "HFP_DELIVER_OTHER");
            }

            if (callback instanceof SimpleHfpCallback) {
                try {
                    java.lang.reflect.Field field =
                            SimpleHfpCallback.class.getDeclaredField("currentDevice");

                    if (field.getType() != BTDevice.class) {
                        throw new IllegalStateException("PHONE_HFP_DEVICE_ABI");
                    }

                    field.setAccessible(true);
                    field.set(callback, device);
                } catch (ReflectiveOperationException error) {
                    throw new IllegalStateException("PHONE_HFP_DEVICE_ABI", error);
                }
            }
        }

        private void command(Runnable action) {
            if (authority) {
                mHandler.post(action);
            }
        }

        public boolean connect(BTDevice value) {
            return Ivt8BTAdapter.this.connect(value);
        }

        public boolean connect(String address) {
            return Ivt8BTAdapter.this.connect(address);
        }

        public boolean disconnect(BTDevice value) {
            return Ivt8BTAdapter.this.disconnect(value);
        }

        public boolean disconnect(String address) {
            return Ivt8BTAdapter.this.disconnect(address);
        }

        public int getConnectionState() {
            return Ivt8BTAdapter.this.getConnectionState();
        }

        public int getConnectionState(BTDevice value) {
            return Ivt8BTAdapter.this.getConnectionState(value);
        }

        public boolean isConnected() {
            return peer.connected();
        }

        public boolean isProfileReady() {
            return true;
        }

        public BTDevice getConnectedDevice() {
            return Ivt8BTAdapter.this.getConnectedDevice();
        }

        public int getCallState() {
            return peer.state();
        }

        public int getNetworkStatus() {
            return peer.connected() ? 1 : 0;
        }

        public int getSignalStrength() {
            return peer.connected() ? 5 : 0;
        }

        public int getBatteryLevel() {
            return peer.connected() ? 5 : 0;
        }

        public String getPhoneNumber(String address) {
            return "";
        }

        public void dial(String number, String address) {
            if (SyntheticPhonePeer.ADDRESS.equals(address)) {
                dial(number);
            }
        }

        public void hangup(int id) {
            if (id == 1) {
                hangup();
            }
        }

        public String getPhoneNumber() {
            return "";
        }

        public List<Map<String, Object>> getCurrentCalls() {
            List<Map<String, Object>> calls = new ArrayList<>();

            if (peer.state() != SyntheticPhonePeer.TERMINATED) {
                Map<String, Object> call = new HashMap<>();
                call.put("call_id", 1);
                call.put("call_state", peer.state());
                call.put("call_number", peer.number());
                calls.add(call);
            }

            return calls;
        }

        public List<BTDevice> getConnectedDevices() {
            return peer.connected()
                    ? java.util.Collections.<BTDevice>singletonList(device)
                    : new ArrayList<BTDevice>();
        }

        public boolean isHfpServiceConnected() {
            return true;
        }

        public List<BluetoothCall> getCurrentBluetoothCalls() {
            List<BluetoothCall> calls = new ArrayList<>();

            if (peer.state() != SyntheticPhonePeer.TERMINATED) {
                calls.add(
                        new BluetoothCall(
                                device,
                                1,
                                false,
                                peer.number(),
                                peer.outgoingCall(),
                                peer.state(),
                                131));
            }

            return calls;
        }

        public void dial(String number) {
            command(() -> {
                try {
                    peer.dial(peer.resolveOemDialNumber(number));
                    callChanged();
                } catch (IllegalArgumentException | IllegalStateException invalid) {
                    /* rejected without state changes */
                }
            });
        }

        public void answerCall() {
            command(() -> {
                if (peer.connected() && peer.state() == 130) {
                    peer.answer();
                    callChanged();
                }
            });
        }

        public void hangup() {
            command(() -> {
                if (peer.connected() && peer.state() != 131) {
                    peer.hangup();
                    callChanged();
                }
            });
        }

        public boolean isAudioOn() {
            return false;
        }

        public boolean isBandRingtone() {
            return false;
        }

        public void connectAudio() {}

        public void disConnectAudio() {}

        public void openSse() {}

        public void closeSse() {}

        public void transferDTMF(char value) {}

        public void hangupCallAndActiveOtherCall() {}

        public void holdCallAndActiveOtherCall() {}

        public void resumeCallAndRejectOtherCall() {}

        public boolean scoAudioSwitch(BTDevice value, boolean enable) {
            return false;
        }

        public void startScoUsingVirtualVoiceCall(BTDevice value) {}

        public void stopScoUsingVirtualVoiceCall(BTDevice value) {}

        public boolean startVoiceRecognition(BTDevice value) {
            return false;
        }

        public boolean stopVoiceRecognition(BTDevice value) {
            return false;
        }
    }

    private final class PeerPbap extends Pbap {
        private final Map<String, List<IPbapCallback>> callbacks = new HashMap<>();
        private boolean connected;
        private int syncState = 95;
        private final ClassLoader phoneClassLoader;
        private final java.util.concurrent.atomic.AtomicLong
                syncEpoch = new java.util.concurrent.atomic.AtomicLong(),
                contactRequest = new java.util.concurrent.atomic.AtomicLong();
        private boolean callLogPending;

        PeerPbap(Context context, BTDeviceManager manager) {
            super(context, manager);
            phoneClassLoader = context.getClassLoader();
        }

        private List<IPbapCallback> listeners() {
            Set<IPbapCallback> result = new java.util.LinkedHashSet<>();

            if (callbacks.containsKey("all")) {
                result.addAll(callbacks.get("all"));
            }

            if (callbacks.containsKey(SyntheticPhonePeer.ADDRESS)) {
                result.addAll(callbacks.get(SyntheticPhonePeer.ADDRESS));
            }

            return new ArrayList<>(result);
        }

        public void registerListener(IPbapCallback callback) {
            registerListener(callback, "all");
        }

        public void registerListener(IPbapCallback callback, String address) {
            if (callback == null
                    || !("all".equals(address) || SyntheticPhonePeer.ADDRESS.equals(address))) {
                return;
            }

            List<IPbapCallback> values = callbacks.get(address);

            if (values == null) {
                values = new ArrayList<>();
                callbacks.put(address, values);
            }

            if (values.contains(callback)) {
                return;
            }

            values.add(callback);
            mHandler.post(() -> {
                if (listeners().contains(callback)) {
                    callback.onReady();

                    if (connected) {
                        callback.onStateChanged(device, 19, 16);
                    }
                }
            });
        }

        public void unregisterListener(IPbapCallback callback) {
            unregisterListener(callback, "all");
        }

        public void unregisterListener(IPbapCallback callback, String address) {
            if (callbacks.containsKey(address)) {
                callbacks.get(address).remove(callback);
            }
        }

        void setConnected(boolean value) {
            if (connected == value) {
                return;
            }

            int before = connected ? 16 : 19;
            connected = value;
            syncEpoch.incrementAndGet();
            contactRequest.incrementAndGet();

            for (IPbapCallback c : listeners()) {
                c.onStateChanged(device, before, value ? 16 : 19);
            }
        }

        public boolean connect(BTDevice value) {
            if (!authority || !isPeer(value) || !peer.connected()) {
                return false;
            }

            mHandler.post(() -> setConnected(true));

            return true;
        }

        public boolean connect(String address) {
            return SyntheticPhonePeer.ADDRESS.equals(address) && connect(device);
        }

        public boolean disconnect(BTDevice value) {
            if (!authority || !isPeer(value)) {
                return false;
            }

            mHandler.post(() -> setConnected(false));

            return true;
        }

        public boolean disconnect(String address) {
            return SyntheticPhonePeer.ADDRESS.equals(address) && disconnect(device);
        }

        public int getConnectionState() {
            return connected ? 16 : 19;
        }

        public int getConnectionState(BTDevice value) {
            return isPeer(value) ? getConnectionState() : 19;
        }

        public boolean isConnected() {
            return connected;
        }

        public boolean isProfileReady() {
            return true;
        }

        public int getDataResponseType() {
            return 176;
        }

        public boolean cancelSync() {
            if (!authority || !connected) {
                return false;
            }

            syncEpoch.incrementAndGet();
            contactRequest.incrementAndGet();
            syncState = 95;

            return true;
        }

        private void bindSyncDevice(IPbapCallback callback) {
            if (!(callback instanceof SimplePbapCallback)) {
                return;
            }

            try {
                java.lang.reflect.Field field =
                        SimplePbapCallback.class.getDeclaredField("currentSyncDevice");

                if (field.getType() != BTDevice.class) {
                    throw new IllegalStateException("PHONE_PBAP_DEVICE_ABI");
                }

                field.setAccessible(true);
                field.set(callback, device);
            } catch (ReflectiveOperationException error) {
                throw new IllegalStateException("PHONE_PBAP_DEVICE_ABI", error);
            }
        }

        private boolean callLogWorkerRunning() throws ReflectiveOperationException {
            Class<?> type =
                    Class.forName(
                            "com.qinggan.bluetoothphone.logic.manager.CacheCallDataManager",
                            true,
                            phoneClassLoader);
            java.lang.reflect.Method getter = type.getMethod("getInstance");

            if (!java.lang.reflect.Modifier.isStatic(getter.getModifiers())
                    || getter.getReturnType() != type) {
                throw new IllegalStateException("PHONE_PBAP_WORKER_ABI");
            }

            Object manager = getter.invoke(null);

            if (manager == null || !type.isInstance(manager)) {
                throw new IllegalStateException("PHONE_PBAP_WORKER_ABI");
            }

            java.lang.reflect.Field running = type.getDeclaredField("isCallLogInserting");

            if (running.getType() != boolean.class
                    || java.lang.reflect.Modifier.isStatic(running.getModifiers())) {
                throw new IllegalStateException("PHONE_PBAP_WORKER_ABI");
            }

            running.setAccessible(true);

            return running.getBoolean(manager);
        }

        /** Waits for the OEM call log worker; false defers or ends this contact sync. */
        private boolean callLogQuiet(long epoch, long request, long deadline) {
            try {
                // OEM insertCallLog writes true inline on this handler before
                // startContactsSync is queued. Its worker clears false after
                // cleanup (also on failure): this is quiescence, not DB success.
                if (callLogWorkerRunning()) {
                    if (android.os.SystemClock.elapsedRealtime() >= deadline) {
                        syncState = 98;
                        contactRequest.incrementAndGet();

                        return false;
                    }

                    mHandler.postDelayed(() -> contactsReady(epoch, request, deadline), 25);

                    return false;
                }
            } catch (ReflectiveOperationException | IllegalStateException failure) {
                syncState = 98;
                contactRequest.incrementAndGet();

                return false;
            }

            callLogPending = false;

            return true;
        }

        private void contactsReady(long epoch, long request, long deadline) {
            if (!connected
                    || !peer.connected()
                    || epoch != syncEpoch.get()
                    || request != contactRequest.get()) {
                return;
            }

            if (callLogPending && !callLogQuiet(epoch, request, deadline)) {
                return;
            }

            for (IPbapCallback c : listeners()) {
                bindSyncDevice(c);
            }

            syncState = 96;

            for (IPbapCallback c : listeners()) {
                c.onSyncStateChanged(95, 96);
            }

            for (IPbapCallback c : listeners()) {
                c.onContactItemCountUpdated(0, peer.contacts().size());
            }

            for (SyntheticPhonePeer.Contact value : peer.contacts()) {
                BTContactItem item = new BTContactItem(value.name);
                item.numbers.add(new BTPhoneNumber(160, value.number));

                for (IPbapCallback c : listeners()) {
                    c.onContactItemFetched(item);
                }
            }

            syncState = 97;
            peer.record("pbap_contacts");

            for (IPbapCallback c : listeners()) {
                c.onSyncStateChanged(96, 97);
            }
        }

        public boolean startContactsSync() {
            if (!authority || !peer.connected() || !connected) {
                return false;
            }

            final long epoch = syncEpoch.get(), request = contactRequest.incrementAndGet();
            mHandler.post(() ->
                    contactsReady(
                            epoch, request, android.os.SystemClock.elapsedRealtime() + 10000));

            return true;
        }

        public boolean startCallLogSync() {
            if (!authority || !peer.connected() || !connected) {
                return false;
            }

            final long epoch = syncEpoch.incrementAndGet();
            contactRequest.incrementAndGet();
            mHandler.post(() -> {
                if (!peer.connected() || !connected || epoch != syncEpoch.get()) {
                    return;
                }

                callLogPending = true;

                for (IPbapCallback c : listeners()) {
                    c.onSyncStateChanged(95, 96);
                }

                for (IPbapCallback c : listeners()) {
                    bindSyncDevice(c);
                    c.onCallLogItemCountUpdated(0, 0);
                }

                for (IPbapCallback c : listeners()) {
                    c.onSyncStateChanged(96, 97);
                }
            });

            return true;
        }

        public boolean startContactsSyncByRange(int start, int end) {
            return false;
        }

        public boolean startCallLogSyncByRange(int start, int end) {
            if (start < 0 || end < start) {
                return false;
            }

            return startCallLogSync();
        }
    }
}
