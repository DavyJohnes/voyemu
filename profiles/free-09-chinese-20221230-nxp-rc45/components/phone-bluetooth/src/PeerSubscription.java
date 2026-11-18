package com.qinggan.bluetooth.internal.ivt8;

import android.app.ActivityManager;
import android.app.Application;
import android.content.Context;
import android.content.pm.PackageManager;
import android.content.pm.ProviderInfo;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;

import org.json.JSONObject;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.Future;
import java.util.concurrent.ScheduledThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

/**
 * Private read-only Binder capabilities; OEM objects never cross process/classloader boundaries.
 */
final class PeerSubscription {
    static final String PHONE = "com.qinggan.bluetoothphone",
            AUTHORITY = "com.voyemu.input.phone.peer";
    static final String CALLBACK = "com.voyemu.phone.peer.callback.v1";

    interface State {
        String snapshot() throws Exception;

        void accept(JSONObject snapshot) throws Exception;

        void lost();
    }

    private final Context context;
    private final Handler main;
    private final State state;
    private final boolean authority;
    private final Binder lifetime = new Binder();
    private final Map<Integer, Reader> readers = new LinkedHashMap<>();
    private final ScheduledThreadPoolExecutor worker;
    private boolean dispatchQueued;
    private volatile boolean wanted;
    private volatile Client current;
    private final java.util.concurrent.atomic.AtomicLong clientEpoch =
            new java.util.concurrent.atomic.AtomicLong();
    private volatile Client opening;
    private boolean binding;
    private int retry;
    private static final String[] EVENTS = {
        "ROLE_AUTHORITY",
        "ROLE_MIRROR",
        "HFP_REGISTER_PHONE",
        "HFP_REGISTER_OTHER",
        "HFP_DELIVER_PHONE",
        "HFP_DELIVER_OTHER",
        "READ_SUBSCRIBE",
        "READ_REPLACE",
        "READ_UNSUBSCRIBE",
        "READ_DIED",
        "SEND_OK",
        "SEND_DEAD",
        "BIND_PROVIDER",
        "BIND_CALL",
        "BIND_REPLY",
        "BIND_IDENTITY",
        "BIND_OK",
        "BIND_FAIL_PROVIDER",
        "BIND_FAIL_CALL",
        "BIND_FAIL_REPLY",
        "BIND_FAIL_IDENTITY",
        "BIND_FAIL_ATTACH",
        "CALLBACK_BUFFER",
        "CALLBACK_ACCEPT",
        "CALLBACK_REJECT",
        "MIRROR_ACCEPT",
        "MIRROR_REJECT",
        "AUTHORITY_DIED",
        "DETACH",
        "CONSUMER_HFP_CONNECTION_FAILED",
        "CONSUMER_IBT_CONNECTION_FAILED",
        "CONSUMER_HFP_CALL_FAILED",
        "CONSUMER_HFP_READY_FAILED"
    };
    private final java.util.concurrent.atomic.AtomicInteger[] counters =
            new java.util.concurrent.atomic.AtomicInteger[EVENTS.length];
    private final java.util.concurrent.atomic.AtomicInteger logged =
            new java.util.concurrent.atomic.AtomicInteger();
    private volatile String consumerStage = "none", consumerKind = "none";

    void consumerFailure(String stage, RuntimeException error) {
        String kind =
                error instanceof NullPointerException
                        ? "NULL_POINTER"
                        : error instanceof SecurityException
                                ? "SECURITY"
                                : error instanceof UnsupportedOperationException
                                        ? "UNSUPPORTED"
                                        : error instanceof IllegalArgumentException
                                                ? "ILLEGAL_ARGUMENT"
                                                : error instanceof IllegalStateException
                                                        ? "ILLEGAL_STATE"
                                                        : "OTHER_RUNTIME";

        consumerStage = stage;
        consumerKind = kind;
        note("CONSUMER_" + stage + "_FAILED", " kind=" + kind);
    }

    void note(String event) {
        note(event, "");
    }

    private void note(String event, String detail) {
        for (int i = 0; i < EVENTS.length; i++) {
            if (EVENTS[i].equals(event)) {
                counters[i].updateAndGet(value -> value == Integer.MAX_VALUE ? value : value + 1);

                if (logged.getAndUpdate(value -> value < 96 ? value + 1 : 96) < 96) {
                    android.util.Log.i(
                            "VoyemuPhonePeer",
                            event
                                    + detail
                                    + " uid="
                                    + android.os.Process.myUid()
                                    + " pid="
                                    + android.os.Process.myPid());
                }

                return;
            }
        }

        throw new IllegalArgumentException("PHONE_PEER_DIAGNOSTIC_EVENT");
    }

    JSONObject diagnostics() throws Exception {
        JSONObject value = new JSONObject();

        value.put("schema", 1)
                .put("authority", authority)
                .put("wanted", wanted)
                .put("bound", current != null)
                .put("consumer_stage", consumerStage)
                .put("consumer_kind", consumerKind);

        synchronized (readers) {
            value.put("readers", readers.size());
        }

        JSONObject events = new JSONObject();

        for (int i = 0; i < EVENTS.length; i++) {
            events.put(EVENTS[i], counters[i].get());
        }

        return value.put("events", events);
    }

    PeerSubscription(Context context, Handler main, State state, boolean authority) {
        this.context = context.getApplicationContext();
        this.main = main;
        this.state = state;
        this.authority = authority;
        worker =
                new ScheduledThreadPoolExecutor(
                        1,
                        task -> {
                            Thread thread = new Thread(task, "VoyemuPhonePeer");
                            thread.setDaemon(true);

                            return thread;
                        });
        worker.setRemoveOnCancelPolicy(true);

        for (int i = 0; i < counters.length; i++) {
            counters[i] = new java.util.concurrent.atomic.AtomicInteger();
        }

        note(authority ? "ROLE_AUTHORITY" : "ROLE_MIRROR");
    }

    static boolean authoritative(Context context) {
        // Application process identity cannot be changed by createPackageContext or a shared UID
        // Context.
        return PHONE.equals(Application.getProcessName())
                && process(context, android.os.Process.myUid(), android.os.Process.myPid(), PHONE);
    }

    static boolean process(Context context, int uid, int pid, String expected) {
        try {
            ActivityManager manager =
                    (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);

            if (manager == null || pid <= 0) {
                return false;
            }

            java.util.List<ActivityManager.RunningAppProcessInfo> processes =
                    manager.getRunningAppProcesses();

            if (processes == null) {
                return false;
            }

            ActivityManager.RunningAppProcessInfo found = null;

            for (ActivityManager.RunningAppProcessInfo value : processes) {
                if (value.pid == pid) {
                    if (found != null) {
                        return false;
                    }

                    found = value;
                }
            }

            return found != null
                    && found.uid == uid
                    && expected.equals(found.processName)
                    && found.pkgList != null
                    && found.pkgList.length == 1
                    && expected.equals(found.pkgList[0])
                    && context.getPackageManager().getApplicationInfo(expected, 0).uid == uid
                    && context.getPackageManager().checkSignatures(expected, PHONE)
                            == PackageManager.SIGNATURE_MATCH;
        } catch (Exception unavailable) {
            // checkstyle-allow IllegalCatch: any failed identity read fails this check closed
            return false;
        }
    }

    // Compares identity on purpose: the same Binder, token or instance, never an equal one.
    @SuppressWarnings("ReferenceEquality")
    Bundle subscribe(IBinder callback, int uid, int pid) throws Exception {
        if (!authority || callback == null || !callback.isBinderAlive() || pid <= 0) {
            throw new SecurityException("PHONE_PEER_SUBSCRIBE");
        }

        String snapshot = bounded(state.snapshot());
        note("READ_SUBSCRIBE");
        Reader reader = new Reader(callback, uid, pid);

        synchronized (readers) {
            Reader previous = readers.get(pid);

            if (previous != null && previous.uid == uid && previous.callback == callback) {
                reader = previous;
            } else {
                if (previous == null && readers.size() >= 4) {
                    throw new IllegalStateException("PHONE_PEER_READERS");
                }

                callback.linkToDeath(reader, 0);
                readers.put(pid, reader);

                if (previous != null) {
                    note("READ_REPLACE");
                    previous.callback.unlinkToDeath(previous, 0);
                }
            }
        }

        Bundle reply = new Bundle();
        reply.putInt("schema", 1);
        reply.putInt("server_pid", android.os.Process.myPid());
        reply.putInt("server_uid", android.os.Process.myUid());
        reply.putBinder("authority", lifetime);
        reply.putString("snapshot", snapshot);

        return reply;
    }

    // Compares identity on purpose: the same Binder, token or instance, never an equal one.
    @SuppressWarnings("ReferenceEquality")
    void unsubscribe(IBinder callback, int uid, int pid) {
        synchronized (readers) {
            Reader reader = readers.get(pid);

            if (reader != null && reader.uid == uid && reader.callback == callback) {
                readers.remove(pid);
                callback.unlinkToDeath(reader, 0);
                note("READ_UNSUBSCRIBE");
            }
        }
    }

    void publish() {
        if (!authority) {
            return;
        }

        final String snapshot;

        try {
            snapshot = bounded(state.snapshot());
        } catch (Exception invalid) {
            // checkstyle-allow IllegalCatch: an unreadable own snapshot breaks the authority
            // invariant
            throw new IllegalStateException("PHONE_PEER_SNAPSHOT", invalid);
        }

        synchronized (readers) {
            for (Reader reader : readers.values()) {
                reader.pending = snapshot;
            }

            if (dispatchQueued || readers.isEmpty()) {
                return;
            }

            dispatchQueued = true;
        }

        worker.execute(this::dispatch);
    }

    private void dispatch() {
        while (true) {
            Reader selected = null;
            String pending = null;

            synchronized (readers) {
                for (Reader reader : readers.values()) {
                    if (reader.pending != null) {
                        selected = reader;
                        pending = reader.pending;
                        reader.pending = null;
                        break;
                    }
                }

                if (selected == null) {
                    dispatchQueued = false;

                    return;
                }
            }

            Parcel data = Parcel.obtain();

            try {
                data.writeInterfaceToken(CALLBACK);
                data.writeString(pending);

                if (!selected.callback.transact(1, data, null, IBinder.FLAG_ONEWAY)) {
                    note("SEND_DEAD");
                    selected.binderDied();
                } else {
                    note("SEND_OK");
                }
            } catch (RemoteException dead) {
                note("SEND_DEAD");
                selected.binderDied();
            } finally {
                data.recycle();
            }
        }
    }

    private final class Reader implements IBinder.DeathRecipient {
        final IBinder callback;
        final int uid, pid;
        String pending;

        Reader(IBinder callback, int uid, int pid) {
            this.callback = callback;
            this.uid = uid;
            this.pid = pid;
        }

        @Override
        public void binderDied() {
            synchronized (readers) {
                if (readers.get(pid) == this) {
                    readers.remove(pid);
                    note("READ_DIED");
                }
            }
        }
    }

    boolean requestPower(boolean enabled) {
        if (authority
                || !process(
                        context,
                        android.os.Process.myUid(),
                        android.os.Process.myPid(),
                        "com.qinggan.app.setting")) {
            return false;
        }

        worker.execute(() -> {
            try {
                ProviderInfo provider =
                        context.getPackageManager().resolveContentProvider(AUTHORITY, 0);

                if (provider == null
                        || !PHONE.equals(provider.packageName)
                        || provider.applicationInfo == null
                        || context.getPackageManager().checkSignatures(provider.packageName, PHONE)
                                != PackageManager.SIGNATURE_MATCH) {
                    throw new SecurityException("PHONE_POWER_AUTHORITY");
                }

                Bundle args = new Bundle();
                args.putInt("schema", 1);
                args.putBoolean("enabled", enabled);
                Bundle reply =
                        context.getContentResolver()
                                .call(Uri.parse("content://" + AUTHORITY), "set_power", null, args);

                if (reply == null
                        || reply.keySet().size() != 4
                        || !Integer.valueOf(1).equals(reply.get("schema"))
                        || !Boolean.valueOf(enabled).equals(reply.get("enabled"))
                        || !(reply.get("server_uid") instanceof Integer)
                        || !(reply.get("server_pid") instanceof Integer)
                        || provider.applicationInfo.uid != reply.getInt("server_uid")
                        || !process(
                                context,
                                reply.getInt("server_uid"),
                                reply.getInt("server_pid"),
                                PHONE)) {
                    throw new SecurityException("PHONE_POWER_REPLY");
                }

                listen(true);
            } catch (Exception failure) {
                // checkstyle-allow IllegalCatch: a rejected power request is logged on its worker
                android.util.Log.e("VoyemuPhonePower", "request rejected");
            }
        });

        return true;
    }

    void listen(boolean enabled) {
        if (authority) {
            return;
        }

        wanted = enabled;
        worker.execute(() -> {
            if (enabled) {
                bind();
            } else {
                detach();
            }
        });
    }

    private void detach() {
        note("DETACH");
        Client old = current;
        current = null;
        opening = null;
        long revoked = clientEpoch.incrementAndGet();
        retry = 0;
        main.post(() -> {
            if (clientEpoch.get() == revoked) {
                state.lost();
            }
        });

        if (old != null) {
            old.authority.unlinkToDeath(old, 0);

            try {
                Bundle args = new Bundle();
                args.putInt("schema", 1);
                args.putBinder("callback", old);
                context.getContentResolver()
                        .call(Uri.parse("content://" + AUTHORITY), "unsubscribe", null, args);
            } catch (Exception ignored) {
                // checkstyle-allow IllegalCatch: a failed provider unsubscribe changes nothing here
                /* Authority death already invalidates this private capability. */
            }
        }
    }

    private void bind() {
        if (!wanted || current != null || binding) {
            return;
        }

        binding = true;
        String stage = "PROVIDER";

        try {
            note("BIND_PROVIDER");
            ProviderInfo provider = trustedProvider();
            Client client = new Client(clientEpoch.incrementAndGet());
            opening = client;
            stage = "CALL";
            note("BIND_CALL");
            Bundle reply = subscribe(client);
            stage = "REPLY";
            note("BIND_REPLY");
            requireReply(reply);
            client.uid = reply.getInt("server_uid");
            client.pid = reply.getInt("server_pid");
            client.authority = reply.getBinder("authority");
            stage = "IDENTITY";
            note("BIND_IDENTITY");

            if (provider.applicationInfo.uid != client.uid
                    || !process(context, client.uid, client.pid, PHONE)
                    || !client.authority.isBinderAlive()) {
                throw new SecurityException("PHONE_PEER_IDENTITY");
            }

            String snapshot = bounded(reply.getString("snapshot"));
            stage = "ATTACH";
            attach(client, snapshot);
        } catch (Exception unavailable) {
            // checkstyle-allow IllegalCatch: any failed provider bind is noted and retried
            note("BIND_FAIL_" + stage);
            scheduleRetry();
        } finally {
            opening = null;
            binding = false;
        }
    }

    private ProviderInfo trustedProvider() {
        ProviderInfo provider = context.getPackageManager().resolveContentProvider(AUTHORITY, 0);

        if (provider == null
                || !PHONE.equals(provider.packageName)
                || provider.applicationInfo == null
                || context.getPackageManager().checkSignatures(provider.packageName, PHONE)
                        != PackageManager.SIGNATURE_MATCH) {
            throw new SecurityException("PHONE_PEER_AUTHORITY");
        }

        return provider;
    }

    private Bundle subscribe(Client client) {
        Bundle args = new Bundle();

        args.putInt("schema", 1);
        args.putBinder("callback", client);

        return context.getContentResolver()
                .call(Uri.parse("content://" + AUTHORITY), "subscribe", null, args);
    }

    private static void requireReply(Bundle reply) {
        if (reply == null
                || !Integer.valueOf(1).equals(reply.get("schema"))
                || reply.keySet().size() != 5
                || !(reply.get("server_pid") instanceof Integer)
                || !(reply.get("server_uid") instanceof Integer)
                || !(reply.get("snapshot") instanceof String)
                || reply.getBinder("authority") == null) {
            throw new SecurityException("PHONE_PEER_REPLY");
        }
    }

    private void attach(Client client, String snapshot) throws android.os.RemoteException {
        client.authority.linkToDeath(client, 0);

        if (!wanted || client.epoch != clientEpoch.get()) {
            client.authority.unlinkToDeath(client, 0);

            return;
        }

        synchronized (client) {
            current = client;
            opening = null;
            retry = 0;
            note("BIND_OK");
            main.post(() -> mirrorInitial(client, snapshot));

            if (client.early != null && client.earlyUid == client.uid) {
                client.deliver(client.early);
            }

            client.early = null;
        }
    }

    private void mirrorInitial(Client client, String snapshot) {
        if (current == client && client.epoch == clientEpoch.get() && wanted) {
            state.lost();

            try {
                state.accept(new JSONObject(snapshot));
                note("MIRROR_ACCEPT");
            } catch (Exception invalid) {
                // checkstyle-allow IllegalCatch: a rejected snapshot drops the binding, not the
                // looper
                note("MIRROR_REJECT");
                client.binderDied();
            }
        }
    }

    private void scheduleRetry() {
        if (!wanted) {
            return;
        }

        long delay = Math.min(5000, 250L << Math.min(retry++, 4));
        Future<?> unused = worker.schedule(this::bind, delay, TimeUnit.MILLISECONDS);
    }

    private final class Client extends Binder implements IBinder.DeathRecipient {
        final long epoch;
        volatile int uid = -1, pid = -1;
        volatile IBinder authority;
        String early;
        int earlyUid;

        Client(long epoch) {
            this.epoch = epoch;
        }

        @Override
        protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                throws RemoteException {
            // Android 9 libbinder adds TF_ACCEPT_FDS (0x10) remotely; local Binder retains Java
            // flags.
            // No state is accepted until the private capability's provider reply is authenticated.
            if (code != 1
                    || (flags != IBinder.FLAG_ONEWAY && flags != (IBinder.FLAG_ONEWAY | 0x10))
                    || epoch != clientEpoch.get()
                    || !wanted) {
                note("CALLBACK_REJECT");
                throw new SecurityException("PHONE_PEER_CALLBACK");
            }

            data.enforceInterface(CALLBACK);
            String snapshot = bounded(data.readString());

            if (data.dataAvail() != 0) {
                throw new SecurityException("PHONE_PEER_TRAILING");
            }

            synchronized (this) {
                if (opening == this && current != this) {
                    early = snapshot;
                    earlyUid = Binder.getCallingUid();
                    note("CALLBACK_BUFFER");

                    return true;
                }

                if (current != this
                        || Binder.getCallingUid() != uid
                        || authority == null
                        || !authority.isBinderAlive()) {
                    note("CALLBACK_REJECT");
                    throw new SecurityException("PHONE_PEER_CALLBACK");
                }

                note("CALLBACK_ACCEPT");
                deliver(snapshot);

                return true;
            }
        }

        void deliver(String snapshot) {
            main.post(() -> {
                if (current != this || epoch != clientEpoch.get() || !wanted) {
                    return;
                }

                try {
                    state.accept(new JSONObject(snapshot));
                    note("MIRROR_ACCEPT");
                } catch (Exception invalid) {
                    // checkstyle-allow IllegalCatch: a rejected snapshot drops the binding, not the
                    // looper
                    note("MIRROR_REJECT");
                    binderDied();
                }
            });
        }

        // Unchanged from the accepted guest build; the increment is not atomic across threads.
        @SuppressWarnings("NonAtomicVolatileUpdate")
        @Override
        public void binderDied() {
            if (current != this) {
                return;
            }

            note("AUTHORITY_DIED");
            current = null;
            long revoked = clientEpoch.incrementAndGet();
            main.post(() -> {
                if (clientEpoch.get() == revoked) {
                    state.lost();
                }
            });
            worker.execute(PeerSubscription.this::scheduleRetry);
        }
    }

    private static String bounded(String value) {
        if (value == null
                || value.length() > 16384
                || value.getBytes(java.nio.charset.StandardCharsets.UTF_8).length > 16384) {
            throw new IllegalArgumentException("PHONE_PEER_EXTENT");
        }

        return value;
    }
}
