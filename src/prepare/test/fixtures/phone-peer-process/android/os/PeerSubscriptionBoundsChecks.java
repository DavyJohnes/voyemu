package android.os;

import org.json.JSONObject;

import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Supplementary authority tests: genuine adapter mutation, primitive Binder packets only. */
public final class PeerSubscriptionBoundsChecks {
    private static final String TOKEN = "com.voyemu.phone.peer.callback.v1";

    private static void check(boolean ok, String message) {
        if (!ok) {
            throw new AssertionError(message);
        }
    }

    private static final class Recorder extends Binder {
        final List<String> packets = new ArrayList<String>();
        final CountDownLatch entered = new CountDownLatch(1), release;

        Recorder(boolean block) {
            release = new CountDownLatch(block ? 1 : 0);
        }

        @Override
        protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                throws RemoteException {
            check(
                    code == 1
                            && (flags == IBinder.FLAG_ONEWAY
                                    || flags == (IBinder.FLAG_ONEWAY | 0x10)),
                    "callback framing");
            data.enforceInterface(TOKEN);
            String packet = data.readString();
            check(data.dataAvail() == 0, "callback trailing fields");
            check(
                    Binder.getCallingUid() == 1000 && Binder.getCallingPid() == 4101,
                    "actual server source identity");
            entered.countDown();

            try {
                if (!release.await(3, TimeUnit.SECONDS)) {
                    throw new AssertionError("blocked dispatcher deadline");
                }
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
                throw new AssertionError(interrupted);
            }

            synchronized (packets) {
                packets.add(packet);
                packets.notifyAll();
            }

            return true;
        }

        int count() {
            synchronized (packets) {
                return packets.size();
            }
        }

        String last() {
            synchronized (packets) {
                return packets.isEmpty() ? null : packets.get(packets.size() - 1);
            }
        }
    }

    public static void run(Object adapter) throws Exception {
        PeerTestRuntime.enter("launcher");
        Recorder old = new Recorder(false), replacement = new Recorder(false);
        subscribe(adapter, old, 4102);
        IBinder.DeathRecipient staleDeath = death(old);
        subscribe(adapter, replacement, 4102);
        unsubscribe(adapter, old, 4102);
        staleDeath
                .binderDied(); // Already-dispatched death may arrive after unlink and replacement.
        old.kill();
        String latest = control(adapter, "connect");
        expectRevision(replacement, latest);
        check(old.count() == 0, "superseded callback receives new publication");
        replacement.kill();
        control(adapter, "incoming", "{\"number\":\"+70000000001\"}");
        control(adapter, "hangup");
        // Dead registrations must release capacity; same PID replacement must remain possible at
        // capacity.
        List<Recorder> readers = new ArrayList<Recorder>();

        for (int i = 0; i < 4; i++) {
            PeerTestRuntime.enter("launcher");
            Recorder r = new Recorder(false);
            subscribe(adapter, r, 5001 + i);
            readers.add(r);
        }

        expectCapacity(adapter, new Recorder(false), 5005);
        Recorder atCapacity = new Recorder(false);
        subscribe(adapter, atCapacity, 5001);
        unsubscribe(adapter, readers.get(0), 5001);
        deathOrNull(readers.get(0));
        expectCapacity(adapter, new Recorder(false), 5005);
        unsubscribe(adapter, atCapacity, 5001);

        for (int i = 1; i < 4; i++) {
            unsubscribe(adapter, readers.get(i), 5001 + i);
        }

        PeerTestRuntime.enter("launcher");
        Recorder blocked = new Recorder(true);
        subscribe(adapter, blocked, 6001);

        try {
            control(adapter, "incoming", "{\"number\":\"+70000000001\"}");
            check(
                    blocked.entered.await(2, TimeUnit.SECONDS),
                    "dispatcher never reaches controlled boundary");
            long start = System.nanoTime();

            for (int i = 0; i < 12; i++) {
                control(adapter, "answer");
                control(adapter, "hangup");
                control(adapter, "incoming", "{\"number\":\"+70000000002\"}");
            }

            latest = control(adapter, "answer");
            check(
                    System.nanoTime() - start < TimeUnit.SECONDS.toNanos(2),
                    "slow consumer blocks authority transitions");
            check(blocked.count() == 0, "boundary was not blocked");
        } finally {
            blocked.release.countDown();
        }

        expectRevision(blocked, latest);
        Thread.sleep(50);
        check(
                blocked.count() == 2,
                "pending snapshots must coalesce to one latest packet: " + blocked.count());
        JSONObject last = new JSONObject(blocked.last()).getJSONObject("call");
        check(
                last.getInt("state") == 128 && "+70000000002".equals(last.getString("number")),
                "coalesced active +7 state lost");
        unsubscribe(adapter, blocked, 6001);
        System.out.println(
                "PeerSubscriptionBoundsChecks PASS replacement/death, four-reader bound, coalesced"
                        + " latest, nonblocking authority");
    }

    private static void expectCapacity(Object adapter, Recorder reader, int pid) throws Exception {
        try {
            subscribe(adapter, reader, pid);
            throw new AssertionError("fifth distinct live reader accepted");
        } catch (IllegalStateException expected) {
            check("PHONE_PEER_READERS".equals(expected.getMessage()), "wrong capacity failure");
        }
    }

    private static void expectRevision(Recorder reader, String expected) throws Exception {
        long revision = new JSONObject(expected).getLong("sequence"),
                until = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);

        while (System.nanoTime() < until) {
            String last = reader.last();

            if (last != null && new JSONObject(last).getLong("sequence") == revision) {
                return;
            }

            Thread.sleep(2);
        }

        throw new AssertionError("latest authority revision was not delivered");
    }

    private static IBinder.DeathRecipient death(Binder binder) throws Exception {
        IBinder.DeathRecipient value = deathOrNull(binder);

        check(value != null, "fixture must capture actual production death registration");

        return value;
    }

    private static IBinder.DeathRecipient deathOrNull(Binder binder) throws Exception {
        Field field = Binder.class.getDeclaredField("deaths");

        field.setAccessible(true);

        synchronized (binder) {
            List<?> values = (List<?>) field.get(binder);

            return values.isEmpty() ? null : (IBinder.DeathRecipient) values.get(0);
        }
    }

    private static void subscribe(Object adapter, Recorder callback, int pid) throws Exception {
        PeerTestRuntime.enter("phone");

        synchronized (PeerTestRuntime.MAIN_LOCK) {
            invoke(
                    adapter,
                    "subscribePeer",
                    new Class<?>[] {IBinder.class, int.class, int.class},
                    callback,
                    1000,
                    pid);
        }
    }

    private static void unsubscribe(Object adapter, Recorder callback, int pid) throws Exception {
        PeerTestRuntime.enter("phone");

        synchronized (PeerTestRuntime.MAIN_LOCK) {
            invoke(
                    adapter,
                    "unsubscribePeer",
                    new Class<?>[] {IBinder.class, int.class, int.class},
                    callback,
                    1000,
                    pid);
        }
    }

    private static String control(Object adapter, String method) throws Exception {
        return control(adapter, method, "{}");
    }

    private static String control(Object adapter, String method, String data) throws Exception {
        PeerTestRuntime.enter("phone");

        synchronized (PeerTestRuntime.MAIN_LOCK) {
            return (String)
                    invoke(
                            adapter,
                            "control",
                            new Class<?>[] {String.class, String.class},
                            method,
                            data);
        }
    }

    private static Object invoke(Object target, String method, Class<?>[] types, Object... args)
            throws Exception {
        try {
            return target.getClass().getMethod(method, types).invoke(target, args);
        } catch (InvocationTargetException failure) {
            Throwable cause = failure.getCause();

            if (cause instanceof Exception) {
                throw (Exception) cause;
            }

            if (cause instanceof Error) {
                throw (Error) cause;
            }

            throw failure;
        }
    }
}
