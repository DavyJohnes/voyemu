package com.voyemu.input;

import android.app.ActivityManager;
import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Looper;

import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;

/** Authenticated read subscription to the original Phone process adapter. */
public class PhonePeerProvider extends ContentProvider {
    private static final String PHONE = "com.qinggan.bluetoothphone";
    private static final java.util.concurrent.atomic.AtomicInteger DIAGNOSTICS =
            new java.util.concurrent.atomic.AtomicInteger();

    // Fixed codes: 1 entry, 2 main lookup, 3 subscribe, 4 unsubscribe, 5 unavailable, 6 reply;
    // 20/21 server/caller shape, 22/23 signer, 24/25 missing/ambiguous identity, 26 request, 27
    // callback.
    private static void diagnostic(int code, int uid, int pid) {
        int count;

        do {
            count = DIAGNOSTICS.get();

            if (count >= 64) {
                return;
            }
        } while (!DIAGNOSTICS.compareAndSet(count, count + 1));

        android.util.Log.i(
                "VoyemuPhonePeer",
                "code=" + code + (code == 1 ? " uid=" + uid + " pid=" + pid : ""));
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public Bundle call(String operation, String argument, Bundle extras) {
        final int uid = Binder.getCallingUid(), pid = Binder.getCallingPid();

        diagnostic(1, uid, pid);
        authenticate(uid, pid);

        if (argument != null
                || extras == null
                || !("subscribe".equals(operation) || "unsubscribe".equals(operation))
                || !new HashSet<String>(Arrays.asList("schema", "callback")).equals(extras.keySet())
                || !Integer.valueOf(1).equals(extras.get("schema"))
                || !(extras.get("callback") instanceof IBinder)) {
            diagnostic(26, 0, 0);
            throw new IllegalArgumentException("PHONE_PEER_REQUEST");
        }

        final IBinder callback = extras.getBinder("callback");
        final long deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(1000);

        if (!callback.isBinderAlive()) {
            diagnostic(27, 0, 0);
            throw new IllegalArgumentException("PHONE_PEER_CALLBACK");
        }

        try {
            FutureTask<Bundle> task =
                    new FutureTask<Bundle>(() -> peer(operation, callback, uid, pid, deadline));

            return onMain(task);
        } catch (Exception error) {
            // checkstyle-allow IllegalCatch: any failed peer request reports PHONE_PEER_UNAVAILABLE
            diagnostic(5, 0, 0);
            throw new IllegalStateException("PHONE_PEER_UNAVAILABLE", error);
        }
    }

    private Bundle peer(String operation, IBinder callback, int uid, int pid, long deadline)
            throws Exception {
        authenticate(uid, pid);
        diagnostic(2, 0, 0);
        Object adapter = lookupAdapter();

        if (System.nanoTime() >= deadline) {
            throw new IllegalStateException("PHONE_PEER_DEADLINE");
        }

        if ("unsubscribe".equals(operation)) {
            diagnostic(4, 0, 0);
            adapter.getClass()
                    .getMethod("unsubscribePeer", IBinder.class, int.class, int.class)
                    .invoke(adapter, callback, uid, pid);
            Bundle reply = new Bundle();
            reply.putInt("schema", 1);

            return reply;
        }

        diagnostic(3, 0, 0);
        Object value =
                adapter.getClass()
                        .getMethod("subscribePeer", IBinder.class, int.class, int.class)
                        .invoke(adapter, callback, uid, pid);

        if (!(value instanceof Bundle)) {
            throw new IllegalStateException("PHONE_PEER_RESPONSE");
        }

        Bundle reply = (Bundle) value;
        requirePeerReply(reply);
        diagnostic(6, 0, 0);

        return reply;
    }

    private static void requirePeerReply(Bundle reply) {
        if (!new HashSet<String>(
                                Arrays.asList(
                                        "schema",
                                        "server_pid",
                                        "server_uid",
                                        "authority",
                                        "snapshot"))
                        .equals(reply.keySet())
                || !Integer.valueOf(1).equals(reply.get("schema"))
                || !Integer.valueOf(android.os.Process.myPid()).equals(reply.get("server_pid"))
                || !Integer.valueOf(android.os.Process.myUid()).equals(reply.get("server_uid"))
                || !(reply.get("authority") instanceof IBinder)
                || !reply.getBinder("authority").isBinderAlive()
                || !(reply.get("snapshot") instanceof String)
                || reply.getString("snapshot")
                                .getBytes(java.nio.charset.StandardCharsets.UTF_8)
                                .length
                        > 16384) {
            throw new IllegalStateException("PHONE_PEER_RESPONSE");
        }
    }

    private Bundle onMain(FutureTask<Bundle> task) throws Exception {
        Looper main = getContext().getMainLooper();

        if (Looper.myLooper() == main) {
            task.run();
        } else if (!new android.os.Handler(main).post(task)) {
            throw new IllegalStateException("PHONE_PEER_MAIN");
        }

        try {
            return task.get(1000, TimeUnit.MILLISECONDS);
        } catch (InterruptedException error) {
            task.cancel(false);
            Thread.currentThread().interrupt();
            throw error;
        } catch (java.util.concurrent.TimeoutException error) {
            task.cancel(false);
            throw error;
        }
    }

    protected void authenticate(int uid, int pid) {
        ActivityManager manager =
                (ActivityManager)
                        getContext().getSystemService(android.content.Context.ACTIVITY_SERVICE);
        List<ActivityManager.RunningAppProcessInfo> processes =
                manager == null ? null : manager.getRunningAppProcesses();

        verifyProcess(processes, android.os.Process.myUid(), android.os.Process.myPid(), true);
        verifyProcess(processes, uid, pid, false);
    }

    private void verifyProcess(
            List<ActivityManager.RunningAppProcessInfo> processes,
            int uid,
            int pid,
            boolean server) {
        int matches = 0;

        if (processes != null) {
            for (ActivityManager.RunningAppProcessInfo process : processes) {
                if (process.pid == pid) {
                    matches++;

                    if (process.uid != uid
                            || !(server
                                    ? PHONE.equals(process.processName)
                                            && process.pkgList != null
                                            && process.pkgList.length == 1
                                            && PHONE.equals(process.pkgList[0])
                                    : allowedIdentity(process.processName, process.pkgList))) {
                        diagnostic(server ? 20 : 21, 0, 0);
                        throw new SecurityException("PHONE_PEER_PERMISSION");
                    }

                    if (getContext().getPackageManager().checkSignatures(PHONE, process.pkgList[0])
                            != PackageManager.SIGNATURE_MATCH) {
                        diagnostic(server ? 22 : 23, 0, 0);
                        throw new SecurityException("PHONE_PEER_PERMISSION");
                    }
                }
            }
        }

        if (pid <= 0 || uid < 0 || matches != 1) {
            diagnostic(server ? 24 : 25, 0, 0);
            throw new SecurityException("PHONE_PEER_PERMISSION");
        }
    }

    static boolean allowedIdentity(String process, String[] packages) {
        return (PHONE.equals(process)
                        || "com.qinggan.app.launcher".equals(process)
                        || "com.qinggan.canbus.service".equals(process))
                && packages != null
                && packages.length == 1
                && process.equals(packages[0]);
    }

    protected Object lookupAdapter() throws Exception {
        Class<?> type =
                Class.forName(
                        "com.qinggan.bluetooth.BTManager", true, getContext().getClassLoader());
        Object manager =
                type.getMethod("getInstance", android.content.Context.class)
                        .invoke(null, getContext());
        java.lang.reflect.Field field = type.getDeclaredField("mAdapter");

        field.setAccessible(true);
        Object adapter = field.get(manager);

        if (adapter == null) {
            throw new IllegalStateException("PHONE_PEER_ADAPTER");
        }

        return adapter;
    }

    @Override
    public Cursor query(
            Uri uri, String[] projection, String selection, String[] args, String sort) {
        throw new UnsupportedOperationException();
    }

    @Override
    public String getType(Uri uri) {
        throw new UnsupportedOperationException();
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        throw new UnsupportedOperationException();
    }

    @Override
    public int delete(Uri uri, String selection, String[] args) {
        throw new UnsupportedOperationException();
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] args) {
        throw new UnsupportedOperationException();
    }
}
