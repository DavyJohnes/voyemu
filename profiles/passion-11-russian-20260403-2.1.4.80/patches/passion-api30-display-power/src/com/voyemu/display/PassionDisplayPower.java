package com.voyemu.display;

import java.util.HashMap;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Future;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicLong;

/** Private protocol prototype; no installed Android entry point or runtime acceptance. */
final class PassionDisplayPower implements AutoCloseable {
    static final int OFF = 1, ON = 2;

    static final class Device {
        final int logical;
        final String kind;
        final String uniqueId;

        Device(int logical, String kind, String uniqueId) {
            this.logical = logical;
            this.kind = kind;
            this.uniqueId = uniqueId;
        }

        public int logical() {
            return logical;
        }

        public String kind() {
            return kind;
        }

        public String uniqueId() {
            return uniqueId;
        }

        @Override
        public boolean equals(Object object) {
            if (this == object) {
                return true;
            }

            if (!(object instanceof Device)) {
                return false;
            }

            Device other = (Device) object;

            return logical == other.logical
                    && Objects.equals(kind, other.kind)
                    && Objects.equals(uniqueId, other.uniqueId);
        }

        @Override
        public int hashCode() {
            return Objects.hash(logical, kind, uniqueId);
        }
    }

    static final class Proof {
        final String uniqueId;
        final long request;
        final long transaction;
        final int state;
        final boolean applied;
        final long observedMs;

        Proof(
                String uniqueId,
                long request,
                long transaction,
                int state,
                boolean applied,
                long observedMs) {
            this.uniqueId = uniqueId;
            this.request = request;
            this.transaction = transaction;
            this.state = state;
            this.applied = applied;
            this.observedMs = observedMs;
        }

        public String uniqueId() {
            return uniqueId;
        }

        public long request() {
            return request;
        }

        public long transaction() {
            return transaction;
        }

        public int state() {
            return state;
        }

        public boolean applied() {
            return applied;
        }

        public long observedMs() {
            return observedMs;
        }

        @Override
        public boolean equals(Object object) {
            if (this == object) {
                return true;
            }

            if (!(object instanceof Proof)) {
                return false;
            }

            Proof other = (Proof) object;

            return Objects.equals(uniqueId, other.uniqueId)
                    && request == other.request
                    && transaction == other.transaction
                    && state == other.state
                    && applied == other.applied
                    && observedMs == other.observedMs;
        }

        @Override
        public int hashCode() {
            return Objects.hash(uniqueId, request, transaction, state, applied, observedMs);
        }
    }

    static final class Request {
        final long sequence, deadline;
        volatile boolean cancelled;
        Proof completed;

        Request(long sequence, long timeoutMs) {
            this.sequence = sequence;
            deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(timeoutMs);
        }

        void active() {
            if (cancelled || System.nanoTime() >= deadline) {
                throw new IllegalStateException("Request expired; commit forbidden");
            }
        }
    }

    interface Backend {
        Device device(int logical);

        Proof apply(Device device, Request request, int state);

        Proof observe(Device device, Request request);

        void commit(Device device, Request request);

        default void atCommit(Device device, Request request, Runnable completion) {
            completion.run();
        }

        void rollback(Device device, Request request);
    }

    private final Backend backend;
    private final Map<String, Integer> desired = new HashMap<>();
    private final AtomicLong sequence = new AtomicLong();
    private final ThreadPoolExecutor dispatcher =
            new ThreadPoolExecutor(
                    1,
                    1,
                    0,
                    TimeUnit.MILLISECONDS,
                    new ArrayBlockingQueue<>(4),
                    r -> {
                        Thread t = new Thread(r, "voyemu-display-power-owner");
                        t.setDaemon(true);

                        return t;
                    },
                    new ThreadPoolExecutor.AbortPolicy());

    PassionDisplayPower(Backend backend) {
        this.backend = backend;
    }

    static int logical(int setting) {
        if (setting == 2) {
            return 0;
        }

        if (setting == 3) {
            return 1;
        }

        throw new IllegalArgumentException("Unsupported OEM screen setting");
    }

    Device target(int logical) {
        Device d = backend.device(logical);

        if (d == null
                || d.logical() != logical
                || (logical == 0
                        ? !d.kind().equals("LOCAL")
                                || !d.uniqueId().equals("local:8141241408256768")
                        : logical == 1
                                ? !d.kind().equals("VIRTUAL")
                                        || !d.uniqueId()
                                                .equals(
                                                        "virtual:com.android.emulator.multidisplay:1234562")
                                : true)) {
            throw new IllegalStateException("Canonical live display identity required");
        }

        return d;
    }

    Proof request(int uid, int setting, int state) {
        return request(uid, setting, state, 2000);
    }

    Proof request(int uid, int setting, int state, long timeoutMs) {
        if (uid != 0 && uid != 1000) {
            throw new SecurityException("System owner required");
        }

        int logical = logical(setting);

        if ((state != OFF && state != ON) || timeoutMs < 1 || timeoutMs > 5000) {
            throw new IllegalArgumentException("Bad power state/deadline");
        }

        Request request = new Request(sequence.incrementAndGet(), timeoutMs);
        Future<Proof> result = dispatcher.submit(() -> execute(logical, state, request));

        try {
            return result.get(timeoutMs, TimeUnit.MILLISECONDS);
        } catch (TimeoutException timeout) {
            synchronized (request) {
                if (request.completed != null) {
                    return request.completed;
                }

                request.cancelled = true;
            }

            throw new IllegalStateException(
                    "Owner operation timed out; no ACK; pending rollback must finish before next"
                            + " dispatch",
                    timeout);
        } catch (InterruptedException interrupted) {
            synchronized (request) {
                Thread.currentThread().interrupt();

                if (request.completed != null) {
                    return request.completed;
                }

                request.cancelled = true;
            }

            throw new IllegalStateException("Interrupted; no ACK", interrupted);
        } catch (ExecutionException failure) {
            throw new IllegalStateException("Display request failed", failure.getCause());
        }
    }

    // Compares identity on purpose: the same Binder, token or instance, never an equal one.
    @SuppressWarnings("ReferenceEquality")
    private Proof execute(int logical, int state, Request request) {
        request.active();
        Device d = target(logical);

        try {
            Proof applied = backend.apply(d, request, state),
                    observed = backend.observe(d, request);

            if (!valid(applied, d, request, state)
                    || !valid(observed, d, request, state)
                    || observed.transaction() != applied.transaction()) {
                throw new IllegalStateException("Display/state/transaction proof mismatch");
            }

            backend.atCommit(
                    d,
                    request,
                    () -> {
                        synchronized (request) {
                            request.active();
                            backend.commit(d, request);

                            synchronized (desired) {
                                desired.put(d.uniqueId(), state);
                            }

                            request.completed = observed;
                        }
                    });

            synchronized (request) {
                if (request.completed != observed) {
                    throw new IllegalStateException("Completion not linearized");
                }

                return observed;
            }
        } catch (RuntimeException failure) {
            // checkstyle-allow IllegalCatch: dispatcher boundary must preserve cancellation and
            // rollback for arbitrary backend failures.
            try {
                backend.rollback(d, request);
            } catch (RuntimeException rollback) {
                // checkstyle-allow IllegalCatch: dispatcher boundary must preserve cancellation and
                // rollback for arbitrary backend failures.
                failure.addSuppressed(rollback);
            }

            throw failure;
        }
    }

    static boolean valid(Proof p, Device d, Request r, int state) {
        return p != null
                && p.uniqueId().equals(d.uniqueId())
                && p.request() == r.sequence
                && p.transaction() > 0
                && p.applied()
                && p.state() == state;
    }

    int effective(Device d, int global) {
        synchronized (desired) {
            return desired.getOrDefault(d.uniqueId(), global);
        }
    }

    Proof wakeFromOriginalDown(int uid, int logical) {
        return request(uid, logical == 0 ? 2 : logical == 1 ? 3 : -1, ON);
    }

    Object read(java.util.concurrent.Callable<Object> source) {
        try {
            return dispatcher.submit(source).get(2000, TimeUnit.MILLISECONDS);
        } catch (Exception failure) {
            // checkstyle-allow IllegalCatch: dispatcher boundary must preserve cancellation and
            // rollback for arbitrary backend failures.
            throw new IllegalStateException("Bounded actual-state query unavailable", failure);
        }
    }

    @Override
    public void close() {
        dispatcher.shutdown();
    }
}
