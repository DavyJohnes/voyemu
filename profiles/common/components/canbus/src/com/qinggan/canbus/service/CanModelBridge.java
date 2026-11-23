package com.qinggan.canbus.service;

import android.os.SystemClock;
import android.util.Log;

import com.qinggan.canbus.VehicleState;

import java.io.IOException;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;

/**
 * Bridge to the CAN model of the guest: the worker owns all socket I/O and reports the model states
 * and the ACC it observes.
 */
public final class CanModelBridge {
    /** Receives the observed model transitions on the worker thread. */
    public interface Listener {
        void onModelState(
                CanModelBridge source,
                VehicleState state,
                int value,
                long session,
                long epoch,
                long revision);

        // OEM Ex response carries requested/current readback value, not admission status.
        void onModelResponse(
                CanModelBridge source, VehicleState state, int value, long session, long epoch);

        /** Reports a model state command the model admitted, before its response. */
        default void onModelAccepted(CanModelBridge source, VehicleState state, int value) {}

        /** Delivers an observed ACC transition; the receiver rechecks it against getAccStatus. */
        void onModelAcc(CanModelBridge source, int value);
    }

    private static final String TAG = "CanModelBridge";
    private static final long MAX_AGE_MS = 1500;
    private static final Semaphore OWNER = new Semaphore(1);
    private static volatile boolean accUncertain;
    private final ArrayBlockingQueue<Command> queue = new ArrayBlockingQueue<>(16);
    private final CanModelClient client = new CanModelClient();
    private volatile Snapshot snapshot;
    private volatile boolean running, leaseHealthy;
    private volatile Listener listener;
    private Thread worker;
    private boolean recoveryOffAccepted, recoveryArmed, recoveryOnAccepted;
    private int deliveredAcc = -1;
    private int startupAcc = -1;
    private boolean observed;
    private long recoverySession, recoveryEpoch;

    private static final class Snapshot {
        final long session, epoch, revision, at;
        final int warning, forced, acc;

        Snapshot(long s, long e, long r, long at, int w, int f, int a) {
            session = s;
            epoch = e;
            revision = r;
            this.at = at;
            warning = w;
            forced = f;
            acc = a;
        }
    }

    private static final class Command {
        final int op, state, value;
        final long session, epoch;

        Command(int op, int state, int value, Snapshot s) {
            this.op = op;
            this.state = state;
            this.value = value;
            session = s.session;
            epoch = s.epoch;
        }
    }

    private static long pair(int hi, int lo) {
        return ((long) hi << 32) | (lo & 0xffffffffL);
    }

    private Snapshot freshSnapshot() {
        Snapshot s = snapshot;

        return running
                        && leaseHealthy
                        && s != null
                        && SystemClock.elapsedRealtime() - s.at <= MAX_AGE_MS
                ? s
                : null;
    }

    private Snapshot current() {
        return accUncertain ? null : freshSnapshot();
    }

    /** Reads a model state of the current snapshot, -1 without one. */
    public int getVehicleState(VehicleState state) {
        Snapshot s = current();

        if (s == null) {
            return -1;
        }

        if (state == VehicleState.HUM_VSP_FUNCTION_SW) {
            return s.warning;
        }

        if (state == VehicleState.IVI_SOC_MODESET) {
            return s.forced;
        }

        return -1;
    }

    /**
     * Returns the observed ACC state, OFF=0 or ON=2, and -1 while it is unknown: before the first
     * observation, while ACC is uncertain and after the model observation expires. A requested
     * setAccStatus value is not returned until the model reports it. Only the worker exchanges with
     * the model.
     */
    public int getAccStatus() {
        Snapshot s = current();

        return s == null ? -1 : s.acc;
    }

    public boolean isCurrent(long session, long epoch, long revision) {
        Snapshot s = current();

        return s != null && s.session == session && s.epoch == epoch && s.revision == revision;
    }

    public boolean isCurrentEpoch(long session, long epoch) {
        Snapshot s = current();

        return s != null && s.session == session && s.epoch == epoch;
    }

    public void setVehicleState(VehicleState state, int value) {
        if (state == null
                || (state != VehicleState.HUM_VSP_FUNCTION_SW
                        && state != VehicleState.IVI_SOC_MODESET)) {
            return;
        }

        enqueue(2, state.getValue(), value);
    }

    public void setAccStatus(int value) {
        enqueue(3, 0, value);
    }

    /** Retains only an unsent initial host request until the first model lease is ready. */
    public synchronized void setHostAccStatus(int value) {
        if (value != 0 && value != 2) {
            throw new IllegalArgumentException("unknown ACC state");
        }

        if (!observed) {
            startupAcc = value;

            return;
        }

        enqueue(3, 0, value);
    }

    private void enqueue(int op, int state, int value) {
        Snapshot s = op == 3 ? freshSnapshot() : current();

        if (s == null || !queue.offer(new Command(op, state, value, s))) {
            if (op == 3) {
                accUncertain = true;
            }

            Log.w(TAG, "model request unavailable or queue full");
        }
    }

    public static boolean isModelState(VehicleState state) {
        return state == VehicleState.HUM_VSP_FUNCTION_SW || state == VehicleState.IVI_SOC_MODESET;
    }

    public synchronized void start(Listener next) {
        if (running) {
            return;
        }

        listener = next;
        snapshot = null;
        queue.clear();
        observed = false;
        running = true;
        worker = new Thread(this::runWorker, "can-model-bridge");
        worker.start();
    }

    public synchronized void stop() {
        running = false;
        leaseHealthy = false;
        listener = null;
        snapshot = null;
        queue.clear();
        startupAcc = -1;
        observed = true;
        accUncertain = true;
        client.close();

        if (worker != null) {
            worker.interrupt();
        }
    }

    private void runWorker() {
        boolean owned = false;

        try {
            OWNER.acquire();
            owned = true;
            deliveredAcc = -1;

            while (running) {
                try {
                    Command c = queue.poll(500, TimeUnit.MILLISECONDS);

                    if (!running) {
                        break;
                    }

                    if (c != null) {
                        runCommand(c);
                    }

                    if (!pollSnapshot()) {
                        break;
                    }
                } catch (IOException | RuntimeException failure) {
                    // checkstyle-allow IllegalCatch: the model worker outlives any failed exchange
                    snapshot = null;
                    Log.w(TAG, "model transport unavailable; pending outcome unknown", failure);
                } catch (InterruptedException stopped) {
                    if (!running) {
                        break;
                    }
                }
            }
        } catch (InterruptedException stopped) {
            Thread.currentThread().interrupt();
        } finally {
            snapshot = null;

            if (owned) {
                OWNER.release();
            }
        }
    }

    private void runCommand(Command c) throws IOException {
        Snapshot before = c.op == 3 ? freshSnapshot() : current();

        if (before == null || before.session != c.session || before.epoch != c.epoch) {
            if (c.op == 3) {
                accUncertain = true;
            }

            Log.w(TAG, "stale model request");

            return;
        }

        int[] reply = exchange(c);

        if (reply[1] != 0
                || pair(reply[6], reply[7]) != c.session
                || pair(reply[8], reply[9]) != c.epoch) {
            if (c.op == 3) {
                accUncertain = true;
            }

            Log.w(TAG, "model request rejected or session changed");
        }

        if (c.op == 2
                && pair(reply[6], reply[7]) == c.session
                && pair(reply[8], reply[9]) == c.epoch) {
            respond(c, reply[1]);
        }

        if (reply[1] == 0 && accUncertain && c.op == 3) {
            noteRecovery(c);
        }
    }

    /** Sends the command; a failed ACC command leaves ACC uncertain. */
    private int[] exchange(Command c) throws IOException {
        boolean exchanged = false;

        try {
            int[] reply = client.exchange(c.op, c.state, c.value, c.session, c.epoch);
            exchanged = true;

            return reply;
        } finally {
            if (!exchanged && c.op == 3) {
                accUncertain = true;
            }
        }
    }

    private void respond(Command c, int status) {
        VehicleState field =
                c.state == 665 ? VehicleState.HUM_VSP_FUNCTION_SW : VehicleState.IVI_SOC_MODESET;
        int response = status == 0 ? c.value : getVehicleState(field);
        Listener l = listener;

        if (l != null && status == 0) {
            l.onModelAccepted(this, field, c.value);
        }

        if (l != null && response >= 0) {
            l.onModelResponse(this, field, response, c.session, c.epoch);
        }
    }

    private void noteRecovery(Command c) {
        if (c.value == 0) {
            recoveryOffAccepted = true;
            recoveryArmed = false;
            recoveryOnAccepted = false;
            recoverySession = c.session;
            recoveryEpoch = c.epoch;
        } else if (c.value == 2
                && recoveryArmed
                && recoverySession == c.session
                && recoveryEpoch == c.epoch) {
            recoveryOnAccepted = true;
        }
    }

    /** Reads and publishes one model snapshot; returns false when the worker stopped. */
    private boolean pollSnapshot() throws IOException {
        int[] reply = client.exchange(1, 0, 0, 0, 0);

        if (reply[1] != 0) {
            throw new IOException("snapshot rejected");
        }

        long session = pair(reply[6], reply[7]),
                epoch = pair(reply[8], reply[9]),
                revision = pair(reply[10], reply[11]);

        if (session == 0 || epoch == 0 || revision < 0) {
            throw new IOException("invalid identity");
        }

        ensureLease(session, epoch);
        Snapshot old = snapshot;

        if (old != null
                && old.session == session
                && old.epoch == epoch
                && revision < old.revision) {
            throw new IOException("revision moved backwards");
        }

        old = applyAcc(reply[5], session, epoch, old);
        requireStateValues(reply[2], reply[3]);

        if (old != null
                && old.session == session
                && old.epoch == epoch
                && revision > old.revision + 1) {
            Log.w(TAG, "model revision gap; history unavailable");
        }

        Snapshot next =
                new Snapshot(
                        session,
                        epoch,
                        revision,
                        SystemClock.elapsedRealtime(),
                        reply[2],
                        reply[3],
                        reply[5]);

        if (!publishSnapshot(next)) {
            return false;
        }

        if (!accUncertain) {
            notifyModelState(old, next);
            notifyAcc(next.acc);
        }

        return true;
    }

    /** Publishes the first leased snapshot and admits its retained, never-sent host request. */
    private synchronized boolean publishSnapshot(Snapshot next) {
        if (!running) {
            return false;
        }

        snapshot = next;
        observed = true;

        if (startupAcc != -1) {
            int value = startupAcc;
            startupAcc = -1;
            enqueue(3, 0, value);
        }

        return true;
    }

    private static void requireStateValues(int warning, int forced) throws IOException {
        if ((warning != -1 && warning != 1 && warning != 2)
                || (forced != -1 && (forced < 1 || forced > 5))) {
            throw new IOException("invalid state values");
        }
    }

    private void ensureLease(long session, long epoch) throws IOException {
        if (!leaseHealthy) {
            client.acquireLease(session, epoch);
            leaseHealthy = true;
            client.watchLease(() -> {
                leaseHealthy = false;
                snapshot = null;
                accUncertain = true;
            });
        }
    }

    /** Applies the ACC value to the recovery state; returns null when recovery drops history. */
    private Snapshot applyAcc(int acc, long session, long epoch, Snapshot old) throws IOException {
        if (acc != -1 && acc != 0 && acc != 2) {
            throw new IOException("invalid ACC");
        }

        if (acc == -1) {
            accUncertain = true;
        }

        if (accUncertain
                && recoveryOffAccepted
                && acc == 0
                && session == recoverySession
                && epoch == recoveryEpoch) {
            recoveryArmed = true;
        }

        if (accUncertain
                && recoveryOnAccepted
                && recoveryArmed
                && acc == 2
                && session == recoverySession
                && epoch > recoveryEpoch) {
            accUncertain = false;
            recoveryOffAccepted = false;
            recoveryArmed = false;
            recoveryOnAccepted = false;

            return null;
        }

        return old;
    }

    private void notifyAcc(int acc) {
        if (acc == deliveredAcc) {
            return;
        }

        deliveredAcc = acc;
        Listener l = listener;

        if (l != null) {
            l.onModelAcc(this, acc);
        }
    }

    private void notifyModelState(Snapshot old, Snapshot next) {
        Listener l = listener;

        if (l == null) {
            return;
        }

        if (next.warning >= 0
                && (old == null
                        || old.session != next.session
                        || old.epoch != next.epoch
                        || old.warning != next.warning)) {
            l.onModelState(
                    this,
                    VehicleState.HUM_VSP_FUNCTION_SW,
                    next.warning,
                    next.session,
                    next.epoch,
                    next.revision);
        }

        if (next.forced >= 0
                && (old == null
                        || old.session != next.session
                        || old.epoch != next.epoch
                        || old.forced != next.forced)) {
            l.onModelState(
                    this,
                    VehicleState.IVI_SOC_MODESET,
                    next.forced,
                    next.session,
                    next.epoch,
                    next.revision);
        }
    }
}
