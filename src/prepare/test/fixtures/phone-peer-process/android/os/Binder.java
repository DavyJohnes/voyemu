package android.os;

import java.util.ArrayList;
import java.util.List;

public class Binder implements IBinder {
    public static volatile int lastRemoteFlags = -1, lastLocalFlags = -1;
    public static final java.util.concurrent.atomic.AtomicInteger REMOTE_REJECTIONS =
            new java.util.concurrent.atomic.AtomicInteger();
    private final PeerTestRuntime.Identity owner = PeerTestRuntime.CURRENT.get();
    private final List<DeathRecipient> deaths = new ArrayList<DeathRecipient>();
    private volatile boolean alive = true;

    public static int getCallingUid() {
        PeerTestRuntime.Identity caller = PeerTestRuntime.CALLER.get();

        return caller == null ? Process.myUid() : caller.uid;
    }

    public static int getCallingPid() {
        PeerTestRuntime.Identity caller = PeerTestRuntime.CALLER.get();

        return caller == null ? Process.myPid() : caller.pid;
    }

    // Compares Binder identity, as Android does.
    @SuppressWarnings("ReferenceEquality")
    @Override
    public boolean transact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
        if (!alive) {
            throw new RemoteException("dead");
        }

        PeerTestRuntime.Identity old = PeerTestRuntime.CURRENT.get(),
                oldCaller = PeerTestRuntime.CALLER.get();
        PeerTestRuntime.CALLER.set(old);
        PeerTestRuntime.CURRENT.set(owner);
        int deliveredFlags = flags;

        if (old.pid != owner.pid) {
            deliveredFlags |= 0x10;
            lastRemoteFlags = deliveredFlags;
        } else {
            lastLocalFlags = deliveredFlags;
        }

        try {
            return onTransact(code, data, reply, deliveredFlags);
        } catch (RuntimeException receiverFailure) {
            if (old.pid != owner.pid && (flags & IBinder.FLAG_ONEWAY) != 0) {
                REMOTE_REJECTIONS.incrementAndGet();

                return true;
            }

            throw receiverFailure;
        } finally {
            if (this == PeerTestRuntime.callback && PeerTestRuntime.earlyDelivered != null) {
                PeerTestRuntime.earlyDelivered.countDown();
            }

            PeerTestRuntime.CURRENT.set(old);
            PeerTestRuntime.CALLER.set(oldCaller);
        }
    }

    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
            throws RemoteException {
        return false;
    }

    @Override
    public synchronized void linkToDeath(DeathRecipient recipient, int flags)
            throws RemoteException {
        if (!alive) {
            throw new RemoteException("dead");
        }

        deaths.add(recipient);
    }

    @Override
    public synchronized boolean unlinkToDeath(DeathRecipient recipient, int flags) {
        return deaths.remove(recipient);
    }

    @Override
    public boolean isBinderAlive() {
        return alive;
    }

    public void kill() {
        List<DeathRecipient> copy;

        synchronized (this) {
            alive = false;
            copy = new ArrayList<DeathRecipient>(deaths);
            deaths.clear();
        }

        for (DeathRecipient death : copy) {
            death.binderDied();
        }
    }
}
