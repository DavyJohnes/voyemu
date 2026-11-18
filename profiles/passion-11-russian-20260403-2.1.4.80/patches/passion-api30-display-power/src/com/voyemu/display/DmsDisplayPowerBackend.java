package com.voyemu.display;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.IdentityHashMap;
import java.util.Objects;
import java.util.concurrent.atomic.AtomicLong;

/** Private API30 system_server prototype; no installed command route or runtime acceptance. */
public final class DmsDisplayPowerBackend implements PassionDisplayPower.Backend {
    private final Object service, lock, workQueue;
    private final AtomicLong transactions = new AtomicLong();
    private final IdentityHashMap<PassionDisplayPower.Request, Object> owners =
            new IdentityHashMap<>();
    private static final IdentityHashMap<Object, Entry> STATES = new IdentityHashMap<>();

    private static final class Entry {
        Integer committed;
        PassionDisplayPower.Proof proof;
        PassionDisplayPower.Request committedRequest;
        Pending pending;
    }

    private static final class Pending {
        final PassionDisplayPower.Request request;
        int before, target;
        boolean mutated;
        PassionDisplayPower.Proof proof;

        Pending(PassionDisplayPower.Request r, int before, int target) {
            request = r;
            this.before = before;
            this.target = target;
        }
    }

    DmsDisplayPowerBackend(Object service) throws Exception {
        if (service == null
                || !service.getClass()
                        .getName()
                        .equals("com.android.server.display.DisplayManagerService")) {
            throw new IllegalArgumentException("Pinned DMS owner required");
        }

        this.service = service;
        this.lock = field(service, "mSyncRoot");
        this.workQueue = field(service, "mTempDisplayStateWorkQueue");
    }

    static Object field(Object owner, String name) throws Exception {
        for (Class<?> c = owner.getClass(); c != null; c = c.getSuperclass()) {
            try {
                Field f = c.getDeclaredField(name);
                f.setAccessible(true);

                return f.get(owner);
            } catch (NoSuchFieldException next) {
                // The field may be declared by a superclass.
            }
        }

        throw new NoSuchFieldException(name);
    }

    static Object call(Object owner, String name, Class<?>[] types, Object... args)
            throws Exception {
        for (Class<?> c = owner.getClass(); c != null; c = c.getSuperclass()) {
            try {
                Method m = c.getDeclaredMethod(name, types);
                m.setAccessible(true);

                return m.invoke(owner, args);
            } catch (NoSuchMethodException next) {
                // The method may be declared by a superclass.
            }
        }

        throw new NoSuchMethodException(name);
    }

    Object logical(int id) throws Exception {
        return call(field(service, "mLogicalDisplays"), "get", new Class<?>[] {int.class}, id);
    }

    Object deviceObject(int id) throws Exception {
        Object logical = logical(id);

        if (logical == null) {
            throw new IllegalArgumentException("Display absent");
        }

        return call(logical, "getPrimaryDisplayDeviceLocked", new Class<?>[] {});
    }

    static Object info(Object device) throws Exception {
        return call(device, "getDisplayDeviceInfoLocked", new Class<?>[] {});
    }

    @Override
    public PassionDisplayPower.Device device(int id) {
        try {
            synchronized (lock) {
                Object d = deviceObject(id);
                String type = d.getClass().getName();
                String kind =
                        type.equals(
                                        "com.android.server.display.LocalDisplayAdapter$LocalDisplayDevice")
                                ? "LOCAL"
                                : type.equals(
                                                "com.android.server.display.VirtualDisplayAdapter$VirtualDisplayDevice")
                                        ? "VIRTUAL"
                                        : "UNSUPPORTED";

                return new PassionDisplayPower.Device(
                        id, kind, (String) field(info(d), "uniqueId"));
            }
        } catch (Exception e) {
            // checkstyle-allow IllegalCatch: OEM reflection and callbacks must retain rollback and
            // failure context.
            throw new IllegalStateException("Cannot read actual DMS identity", e);
        }
    }

    /**
     * Pending overrides expire and are never published as committed state. Called under DMS lock.
     */
    public static int effectiveState(Object device, int global) throws Exception {
        synchronized (STATES) {
            Entry e = STATES.get(device);

            if (e == null) {
                return global;
            }

            if (e.pending != null
                    && !e.pending.request.cancelled
                    && System.nanoTime() < e.pending.request.deadline) {
                return e.pending.target;
            }

            return e.committed == null ? global : e.committed;
        }
    }

    static boolean pending(Object device) {
        synchronized (STATES) {
            Entry e = STATES.get(device);

            return e != null && e.pending != null;
        }
    }

    private Object exact(PassionDisplayPower.Device target) throws Exception {
        Object d = deviceObject(target.logical());

        if (!device(target.logical()).equals(target)) {
            throw new IllegalStateException("Display replaced");
        }

        return d;
    }

    @Override
    public PassionDisplayPower.Proof apply(
            PassionDisplayPower.Device target, PassionDisplayPower.Request request, int state) {
        try {
            request.active();
            Object d;
            Pending pending;

            synchronized (lock) {
                d = exact(target);
                int before = ((Number) field(info(d), "state")).intValue();

                if (before != 1 && before != 2) {
                    throw new IllegalStateException("Unknown actual power state");
                }

                synchronized (STATES) {
                    Entry entry = STATES.computeIfAbsent(d, key -> new Entry());

                    if (entry.pending != null) {
                        throw new IllegalStateException("Device request already pending");
                    }

                    pending = new Pending(request, before, state);
                    entry.pending = pending;
                    owners.put(request, d);
                }
            }

            PassionDisplayPower.Proof proof = perform(target, request, state, false);

            synchronized (STATES) {
                if (STATES.get(d).pending != pending) {
                    throw new IllegalStateException("Pending owner changed");
                }

                pending.proof = proof;
            }

            return proof;
        } catch (Exception e) {
            // checkstyle-allow IllegalCatch: OEM reflection and callbacks must retain rollback and
            // failure context.
            throw new IllegalStateException("Actual display power application failed", e);
        }
    }

    // A close failure without an earlier failure is the failure of the transaction itself.
    @SuppressWarnings("Finally")
    private PassionDisplayPower.Proof perform(
            PassionDisplayPower.Device target,
            PassionDisplayPower.Request request,
            int state,
            boolean rollback)
            throws Exception {
        Object transaction =
                Class.forName("android.view.SurfaceControl$Transaction")
                        .getDeclaredConstructor()
                        .newInstance();
        Throwable failure = null;

        try {
            if (!rollback) {
                request.active();
            }

            // Match the pinned global path's work-queue -> sync-root order, including hardware
            // Runnable.
            synchronized (workQueue) {
                synchronized (lock) {
                    Object d = admit(target, request, rollback);
                    hardware(target, d, state);

                    if (!rollback) {
                        request.active();
                    }

                    exact(target);
                    call(service, "updateLogicalDisplaysLocked", new Class<?>[] {});
                    Class<?> tx = transaction.getClass(),
                            deviceBase = Class.forName("com.android.server.display.DisplayDevice");
                    call(
                            logical(target.logical()),
                            "configureDisplayLocked",
                            new Class<?>[] {tx, deviceBase, boolean.class},
                            transaction,
                            d,
                            state == PassionDisplayPower.OFF);
                    call(d, "performTraversalLocked", new Class<?>[] {tx}, transaction);
                    call(transaction, "apply", new Class<?>[] {boolean.class}, true);

                    if (!rollback) {
                        request.active();
                    }

                    exact(target);
                    int actual = ((Number) field(info(d), "state")).intValue();

                    if (actual != state) {
                        throw new IllegalStateException(
                                "Actual state did not match applied transaction");
                    }

                    return new PassionDisplayPower.Proof(
                            target.uniqueId(),
                            request.sequence,
                            transactions.incrementAndGet(),
                            actual,
                            true,
                            System.currentTimeMillis());
                }
            }
        } catch (Exception | Error e) {
            // checkstyle-allow IllegalCatch: OEM reflection and callbacks must retain rollback and
            // failure context.
            failure = e;
            throw e;
        } finally {
            try {
                call(transaction, "close", new Class<?>[] {});
            } catch (Exception close) {
                // checkstyle-allow IllegalCatch: OEM reflection and callbacks must retain rollback
                // and failure context.
                if (failure != null) {
                    failure.addSuppressed(close);
                } else {
                    throw close;
                }
            }
        }
    }

    private void hardware(PassionDisplayPower.Device target, Object d, int state) throws Exception {
        float brightness = ((Number) field(service, "mGlobalDisplayBrightness")).floatValue();
        Runnable hardware =
                (Runnable)
                        call(
                                d,
                                "requestDisplayStateLocked",
                                new Class<?>[] {int.class, float.class},
                                state,
                                brightness);

        if (target.kind().equals("VIRTUAL")) {
            call(
                    d,
                    "setDisplayState",
                    new Class<?>[] {boolean.class},
                    state == PassionDisplayPower.ON);
        }

        if (hardware != null) {
            hardware.run();
        }
    }

    private Object admit(
            PassionDisplayPower.Device target,
            PassionDisplayPower.Request request,
            boolean rollback)
            throws Exception {
        if (!rollback) {
            request.active();
        }

        Object d = exact(target);

        if (!rollback) {
            synchronized (STATES) {
                Entry e = STATES.get(d);

                if (e == null || e.pending == null || e.pending.request != request) {
                    throw new IllegalStateException("Pending owner absent");
                }

                e.pending.before = ((Number) field(info(d), "state")).intValue();

                if (e.pending.before != 1 && e.pending.before != 2) {
                    throw new IllegalStateException("Unknown pre-mutation state");
                }

                e.pending.mutated = true;
            }
        }

        return d;
    }

    @Override
    public PassionDisplayPower.Proof observe(
            PassionDisplayPower.Device target, PassionDisplayPower.Request request) {
        try {
            synchronized (lock) {
                Object d = exact(target);
                int actual = ((Number) field(info(d), "state")).intValue();

                synchronized (STATES) {
                    Entry e = STATES.get(d);
                    PassionDisplayPower.Proof proof =
                            e != null && e.pending != null && e.pending.request == request
                                    ? e.pending.proof
                                    : e == null || e.committedRequest != request ? null : e.proof;

                    if (proof == null
                            || proof.request() != request.sequence
                            || !proof.uniqueId().equals(target.uniqueId())) {
                        return null;
                    }

                    return new PassionDisplayPower.Proof(
                            target.uniqueId(),
                            request.sequence,
                            proof.transaction(),
                            actual,
                            proof.applied(),
                            System.currentTimeMillis());
                }
            }
        } catch (Exception e) {
            // checkstyle-allow IllegalCatch: OEM reflection and callbacks must retain rollback and
            // failure context.
            throw new IllegalStateException("Actual display readback failed", e);
        }
    }

    // Compares identity on purpose: the same Binder, token or instance, never an equal one.
    @SuppressWarnings("ReferenceEquality")
    @Override
    public void atCommit(
            PassionDisplayPower.Device target,
            PassionDisplayPower.Request request,
            Runnable completion) {
        // Acquire DMS BEFORE the tiny completion monitor; never wait for DMS while holding request.
        try {
            synchronized (lock) {
                Object live = exact(target);

                synchronized (STATES) {
                    Entry e = STATES.get(live);

                    if (owners.get(request) != live
                            || e == null
                            || e.pending == null
                            || e.pending.request != request
                            || ((Number) field(info(live), "state")).intValue()
                                    != e.pending.target) {
                        throw new IllegalStateException(
                                "Canonical live applied device changed before commit");
                    }
                }

                completion.run();
            }
        } catch (Exception failure) {
            // checkstyle-allow IllegalCatch: OEM reflection and callbacks must retain rollback and
            // failure context.
            throw new IllegalStateException("Live-device completion rejected", failure);
        }
    }

    @Override
    public void commit(PassionDisplayPower.Device target, PassionDisplayPower.Request request) {
        if (!Thread.holdsLock(lock)) {
            throw new IllegalStateException("DMS live-device commit fence required");
        }

        // No DMS/native lock or external call while the request completion monitor is held.
        synchronized (STATES) {
            Object d = owners.get(request);
            Entry e = d == null ? null : STATES.get(d);

            if (e == null
                    || e.pending == null
                    || e.pending.request != request
                    || !PassionDisplayPower.valid(
                            e.pending.proof, target, request, e.pending.target)) {
                throw new IllegalStateException("No exact applied request to commit");
            }

            request.active();
            e.committed = e.pending.target;
            e.proof = e.pending.proof;
            e.committedRequest = request;
            e.pending = null;
            owners.remove(request);
        }
    }

    // Compares identity on purpose: the same Binder, token or instance, never an equal one.
    @SuppressWarnings("ReferenceEquality")
    @Override
    public void rollback(PassionDisplayPower.Device target, PassionDisplayPower.Request request) {
        Object d = null;
        Pending pending = null;

        try {
            synchronized (STATES) {
                d = owners.get(request);
                Entry e = d == null ? null : STATES.get(d);

                if (e == null || e.pending == null || e.pending.request != request) {
                    return;
                }

                pending = e.pending;
                pending.target = pending.before;
            }

            if (!pending.mutated) {
                return;
            }

            synchronized (lock) {
                if (exact(target) != d) {
                    throw new IllegalStateException(
                            "Original device removed; cannot restore hardware");
                }
            }

            perform(target, request, pending.before, true);
        } catch (Exception e) {
            // checkstyle-allow IllegalCatch: OEM reflection and callbacks must retain rollback and
            // failure context.
            throw new IllegalStateException("Actual rollback failed; state unknown, no success", e);
        } finally {
            synchronized (STATES) {
                owners.remove(request);

                if (d != null) {
                    Entry e = STATES.get(d);

                    if (e != null && e.pending == pending) {
                        e.pending = null;

                        if (e.committed == null) {
                            STATES.remove(d);
                        }
                    }
                }
            }
        }
    }

    static final class Snapshot {
        final PassionDisplayPower.Device device;
        final int state;
        final PassionDisplayPower.Proof proof;
        final long order;
        final long observedMs;

        Snapshot(
                PassionDisplayPower.Device device,
                int state,
                PassionDisplayPower.Proof proof,
                long order,
                long observedMs) {
            this.device = device;
            this.state = state;
            this.proof = proof;
            this.order = order;
            this.observedMs = observedMs;
        }

        public PassionDisplayPower.Device device() {
            return device;
        }

        public int state() {
            return state;
        }

        public PassionDisplayPower.Proof proof() {
            return proof;
        }

        public long order() {
            return order;
        }

        public long observedMs() {
            return observedMs;
        }

        @Override
        public boolean equals(Object object) {
            if (this == object) {
                return true;
            }

            if (!(object instanceof Snapshot)) {
                return false;
            }

            Snapshot other = (Snapshot) object;

            return Objects.equals(device, other.device)
                    && state == other.state
                    && Objects.equals(proof, other.proof)
                    && order == other.order
                    && observedMs == other.observedMs;
        }

        @Override
        public int hashCode() {
            return Objects.hash(device, state, proof, order, observedMs);
        }
    }

    Snapshot snapshot(PassionDisplayPower.Device target) {
        try {
            synchronized (lock) {
                Object d = exact(target);
                int state = ((Number) field(info(d), "state")).intValue();

                synchronized (STATES) {
                    Entry e = STATES.get(d);

                    if (e != null && e.pending != null) {
                        throw new IllegalStateException("Device transition still pending");
                    }

                    return new Snapshot(
                            target,
                            state,
                            e == null ? null : e.proof,
                            transactions.incrementAndGet(),
                            System.currentTimeMillis());
                }
            }
        } catch (Exception failure) {
            // checkstyle-allow IllegalCatch: OEM reflection and callbacks must retain rollback and
            // failure context.
            throw new IllegalStateException("Actual state query failed", failure);
        }
    }
}
