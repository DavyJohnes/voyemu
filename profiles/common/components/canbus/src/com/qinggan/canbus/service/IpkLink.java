package com.qinggan.canbus.service;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.SocketAddress;
import java.net.SocketTimeoutException;
import java.nio.ByteBuffer;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.channels.SocketChannel;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;

/**
 * The single owner of the IVC connection from Android to the cluster ivcd: one persistent TCP
 * connection with the OEM heartbeat 0x67 and supervision of its 0x87 reply, bounded reconnection,
 * reading of cluster frames and answers to cluster card requests from the frames last sent. A write
 * that fails is reported and never repeated; publications are not replayed after a reconnection.
 */
final class IpkLink {
    static final SocketAddress IVCD = new InetSocketAddress("172.16.100.22", 80);

    /** Receives cluster frames other than heartbeat replies on the link thread. */
    interface Listener {
        void received(byte[] frame);
    }

    /** Outcome of one publication; called on the link thread. */
    interface Receipt {
        void completed(
                long sequence, boolean submitted, Stage stage, Reason reason, IoClass ioClass);
    }

    enum Stage {
        UNKNOWN,
        CONNECT,
        WRITE,
        COMPLETE
    }

    enum Reason {
        NONE,
        TIMEOUT,
        REFUSED,
        PERMISSION,
        ROUTE,
        CLOSED,
        SUPERVISION,
        OTHER_IO
    }

    enum IoClass {
        NONE,
        TIMEOUT,
        CONNECT,
        NO_ROUTE,
        CLOSED_CHANNEL,
        SOCKET,
        IO
    }

    static Reason reason(IOException failure) {
        if (failure instanceof SocketTimeoutException) {
            return Reason.TIMEOUT;
        }

        if (failure instanceof java.net.NoRouteToHostException) {
            return Reason.ROUTE;
        }

        if (failure instanceof java.nio.channels.ClosedChannelException) {
            return Reason.CLOSED;
        }

        Throwable cause = failure;

        for (int i = 0; cause != null && i < 4; i++, cause = cause.getCause()) {
            if (!"android.system.ErrnoException".equals(cause.getClass().getName())) {
                continue;
            }

            Reason mapped = errnoReason(cause);

            if (mapped != null) {
                return mapped;
            }

            break;
        }

        return Reason.OTHER_IO;
    }

    /** Maps the errno of an ErrnoException; null leaves the failure typed OTHER_IO. */
    private static Reason errnoReason(Throwable cause) {
        try {
            Object raw = cause.getClass().getField("errno").get(cause);

            return raw instanceof Integer ? errnoReason((int) (Integer) raw) : null;
        } catch (ReflectiveOperationException unavailable) {
            return null;
        }
    }

    private static Reason errnoReason(int errno) {
        if (errno == 111) {
            return Reason.REFUSED;
        }

        if (errno == 1 || errno == 13) {
            return Reason.PERMISSION;
        }

        if (errno == 100 || errno == 101 || errno == 112 || errno == 113) {
            return Reason.ROUTE;
        }

        if (errno == 9 || errno == 104 || errno == 107 || errno == 125) {
            return Reason.CLOSED;
        }

        if (errno == 110) {
            return Reason.TIMEOUT;
        }

        return null;
    }

    static IoClass ioClass(IOException failure) {
        if (failure instanceof SocketTimeoutException) {
            return IoClass.TIMEOUT;
        }

        if (failure instanceof java.net.NoRouteToHostException) {
            return IoClass.NO_ROUTE;
        }

        if (failure instanceof java.nio.channels.ClosedChannelException) {
            return IoClass.CLOSED_CHANNEL;
        }

        if (failure instanceof java.net.ConnectException) {
            return IoClass.CONNECT;
        }

        if (failure instanceof java.net.SocketException) {
            return IoClass.SOCKET;
        }

        return IoClass.IO;
    }

    private static final class Publication {
        final long sequence;
        final String kind, slot, origin;
        final byte[][] frames;
        final Receipt receipt;

        Publication(
                long sequence,
                String kind,
                String slot,
                String origin,
                byte[][] frames,
                Receipt receipt) {
            this.sequence = sequence;
            this.kind = kind;
            this.slot = slot;
            this.origin = origin;
            this.frames = frames;
            this.receipt = receipt;
        }
    }

    private final SocketAddress target;
    private final long heartbeatMs, replyTimeoutMs, reconnectMinMs, reconnectMaxMs;
    private final ArrayBlockingQueue<Publication> pending = new ArrayBlockingQueue<>(64);
    private final CanObservationJournal journal = new CanObservationJournal(512);
    private final Map<String, byte[][]> cache = new HashMap<>();
    private final long startedNs = System.nanoTime();
    private volatile Listener listener;
    private Thread worker;
    private volatile boolean stopped;
    private long sequence;
    // Link thread state, published under this lock for the snapshot.
    private SocketChannel channel;
    private Selector selector;
    private final ByteBuffer input = ByteBuffer.allocate(8192);
    private long generation, connects, reconnects, heartbeats, replies, malformed;
    private long nextConnectNs, backoffMs, nextHeartbeatNs, lastReplyNs;
    private Reason lastFailure = Reason.NONE;

    IpkLink(SocketAddress target) {
        this(target, 1000, 3500, 500, 8000);
    }

    IpkLink(
            SocketAddress target,
            long heartbeatMs,
            long replyTimeoutMs,
            long reconnectMinMs,
            long reconnectMaxMs) {
        this.target = target;
        this.heartbeatMs = heartbeatMs;
        this.replyTimeoutMs = replyTimeoutMs;
        this.reconnectMinMs = reconnectMinMs;
        this.reconnectMaxMs = reconnectMaxMs;
        this.backoffMs = reconnectMinMs;
    }

    void listen(Listener value) {
        listener = value;
    }

    /** Starts the link thread once; it keeps the connection until stop(). */
    synchronized void start() {
        if (worker != null || stopped) {
            return;
        }

        worker = new Thread(this::run, "VoyemuIpkLink");
        worker.setDaemon(true);
        worker.start();
    }

    void stop() {
        Thread current;

        synchronized (this) {
            stopped = true;
            current = worker;
        }

        if (current != null) {
            current.interrupt();
        }
    }

    /**
     * Queues frames for one write; slot names the cluster card whose last frames answer a later
     * cluster request. Returns the sequence; a full queue rejects the publication.
     */
    synchronized long send(
            String kind, String slot, String origin, byte[][] frames, Receipt receipt) {
        if (frames.length == 0) {
            return 0;
        }

        for (byte[] frame : frames) {
            if (IpkFrames.command(frame) < 0) {
                throw new IllegalArgumentException("ipk frame");
            }
        }

        if (sequence == Long.MAX_VALUE) {
            throw new IllegalStateException("ipk sequence");
        }

        Publication publication =
                new Publication(sequence + 1, kind, slot, origin, frames.clone(), receipt);

        if (!pending.offer(publication)) {
            record("sent", publication, publication.frames[0], "dropped", Reason.NONE);
            throw new IllegalStateException("ipk queue full");
        }

        return ++sequence;
    }

    private void run() {
        try {
            while (!stopped) {
                long now = System.nanoTime();

                if (channel == null && now - nextConnectNs >= 0) {
                    connect();
                }

                long wait =
                        channel == null ? 50 : Math.max(1, Math.min(50, millis(nextHeartbeatNs)));
                Publication publication = pending.poll(wait, TimeUnit.MILLISECONDS);

                if (publication != null) {
                    deliver(publication);
                }

                if (channel != null) {
                    supervise();
                }

                if (channel != null) {
                    read();
                }
            }
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
        } finally {
            disconnect(Reason.CLOSED);
        }
    }

    private static long millis(long deadlineNs) {
        return (deadlineNs - System.nanoTime()) / 1000000;
    }

    private void connect() {
        SocketChannel opened = null;
        Selector waiting = null;

        try {
            opened = SocketChannel.open();
            waiting = Selector.open();
            opened.configureBlocking(false);
            boolean connected = opened.connect(target);
            opened.register(waiting, SelectionKey.OP_CONNECT);
            long deadline = System.nanoTime() + 1500000000L;

            while (!connected) {
                await(waiting, deadline);
                connected = opened.finishConnect();
            }

            synchronized (this) {
                channel = opened;
                selector = waiting;
                generation++;

                if (connects++ > 0) {
                    reconnects++;
                }

                backoffMs = reconnectMinMs;
                lastReplyNs = System.nanoTime();
                nextHeartbeatNs = lastReplyNs;
                input.clear();
            }
        } catch (IOException failure) {
            close(opened, waiting);

            synchronized (this) {
                lastFailure = reason(failure);
                nextConnectNs = System.nanoTime() + backoffMs * 1000000;
                backoffMs = Math.min(reconnectMaxMs, backoffMs * 2);
            }
        }
    }

    private static void close(SocketChannel opened, Selector waiting) {
        try {
            if (opened != null) {
                opened.close();
            }

            if (waiting != null) {
                waiting.close();
            }
        } catch (IOException ignored) {
            // The descriptor is released or the link is already down.
        }
    }

    private static void await(Selector waiting, long deadline) throws IOException {
        long remaining = deadline - System.nanoTime();

        if (remaining <= 0) {
            throw new SocketTimeoutException("ipk link timeout");
        }

        waiting.select(Math.max(1, (remaining + 999999) / 1000000));
        waiting.selectedKeys().clear();
    }

    private synchronized void disconnect(Reason reason) {
        if (channel == null) {
            return;
        }

        close(channel, selector);
        channel = null;
        selector = null;
        lastFailure = reason;
        nextConnectNs = System.nanoTime() + backoffMs * 1000000;
    }

    private void write(byte[] frame) throws IOException {
        ByteBuffer bytes = ByteBuffer.wrap(frame);
        long deadline = System.nanoTime() + 1500000000L;
        SelectionKey key = channel.keyFor(selector);

        key.interestOps(SelectionKey.OP_WRITE);

        while (bytes.hasRemaining()) {
            if (channel.write(bytes) == 0) {
                await(selector, deadline);
            }
        }
    }

    private void deliver(Publication publication) {
        if (channel == null) {
            Reason reason;

            synchronized (this) {
                reason = lastFailure == Reason.NONE ? Reason.CLOSED : lastFailure;
            }

            complete(publication, false, Stage.CONNECT, reason, IoClass.NONE);

            return;
        }

        try {
            for (byte[] frame : publication.frames) {
                write(frame);
            }

            if (publication.slot != null) {
                synchronized (this) {
                    cache.put(publication.slot, publication.frames);
                }
            }

            complete(publication, true, Stage.COMPLETE, Reason.NONE, IoClass.NONE);
        } catch (IOException failure) {
            Reason reason = reason(failure);

            disconnect(reason);
            complete(publication, false, Stage.WRITE, reason, ioClass(failure));
        }
    }

    private void complete(
            Publication publication,
            boolean submitted,
            Stage stage,
            Reason reason,
            IoClass ioClass) {
        record(
                "sent",
                publication,
                publication.frames[0],
                submitted ? "submitted" : "failed",
                reason);

        if (publication.receipt != null) {
            publication.receipt.completed(publication.sequence, submitted, stage, reason, ioClass);
        }
    }

    private void supervise() {
        long now = System.nanoTime();

        if ((now - lastReplyNs) / 1000000 > replyTimeoutMs) {
            disconnect(Reason.SUPERVISION);

            return;
        }

        if (now - nextHeartbeatNs >= 0) {
            try {
                write(IpkFrames.heartbeat());

                synchronized (this) {
                    heartbeats++;
                    nextHeartbeatNs = now + heartbeatMs * 1000000;
                }
            } catch (IOException failure) {
                disconnect(reason(failure));
            }
        }
    }

    private void read() {
        try {
            int count = channel.read(input);

            if (count < 0) {
                disconnect(Reason.CLOSED);

                return;
            }
        } catch (IOException failure) {
            disconnect(reason(failure));

            return;
        }

        input.flip();

        while (input.remaining() >= 9) {
            int at = input.position();

            if (input.get(at) != 10) {
                input.get();
                malformed++;
                continue;
            }

            int total = (((input.get(at + 3) & 255) << 8) | (input.get(at + 4) & 255)) + 7;

            if (total > input.capacity()) {
                input.get();
                malformed++;
                continue;
            }

            if (input.remaining() < total) {
                break;
            }

            if (input.get(at + total - 1) != 13) {
                input.get();
                malformed++;
                continue;
            }

            byte[] frame = new byte[total];
            input.get(frame);
            received(frame);
        }

        input.compact();
    }

    private void received(byte[] frame) {
        int cmd = frame[5] & 255;

        if (cmd == 0x87 || cmd == 0x86) {
            synchronized (this) {
                replies++;
                lastReplyNs = System.nanoTime();
            }

            return;
        }

        String slot = cmd == 0x80 ? "navi" : cmd == 0x81 ? "media" : cmd == 0x83 ? "phone" : null;
        byte[][] cached;

        synchronized (this) {
            record("received", null, frame, "received", Reason.NONE);
            cached = slot != null && frame.length > 6 && frame[6] == 1 ? cache.get(slot) : null;
        }

        if (cached != null) {
            long next;

            synchronized (this) {
                next = sequence == Long.MAX_VALUE ? 0 : ++sequence;
            }

            if (next != 0) {
                deliver(new Publication(next, slot, null, "request", cached, null));
            }
        }

        Listener current = listener;

        if (current != null) {
            current.received(frame.clone());
        }
    }

    private synchronized void record(
            String direction,
            Publication publication,
            byte[] frame,
            String outcome,
            Reason reason) {
        int cmd = frame[5] & 255;

        journal.append(
                "{\"seq\":\""
                        + journal.next()
                        + "\",\"elapsed_ms\":"
                        + (System.nanoTime() - startedNs) / 1000000
                        + ",\"direction\":\""
                        + direction
                        + "\",\"cmd\":\""
                        + String.format(Locale.ROOT, "0x%02x", cmd)
                        + "\",\"kind\":\""
                        + (publication == null ? "cluster" : publication.kind)
                        + "\",\"origin\":\""
                        + (publication == null ? "ivcd" : publication.origin)
                        + "\",\"frames\":"
                        + (publication == null ? 1 : publication.frames.length)
                        + ",\"bytes\":"
                        + bytes(publication, frame)
                        + ",\"outcome\":\""
                        + outcome
                        + "\",\"reason\":\""
                        + reason.name().toLowerCase(Locale.ROOT)
                        + "\",\"generation\":"
                        + generation
                        + "}");
    }

    private static int bytes(Publication publication, byte[] frame) {
        if (publication == null) {
            return frame.length;
        }

        int total = 0;

        for (byte[] value : publication.frames) {
            total += value.length;
        }

        return total;
    }

    /** Link state without frame contents; the history carries no phone or media text. */
    synchronized String status() {
        return "{\"state\":\""
                + (stopped
                        ? "stopped"
                        : channel != null ? "connected" : worker == null ? "idle" : "connecting")
                + "\",\"generation\":"
                + generation
                + ",\"reconnects\":"
                + reconnects
                + ",\"heartbeats_sent\":"
                + heartbeats
                + ",\"heartbeat_replies\":"
                + replies
                + ",\"last_reply_age_ms\":"
                + (channel == null
                        ? "null"
                        : String.valueOf((System.nanoTime() - lastReplyNs) / 1000000))
                + ",\"malformed\":"
                + malformed
                + ",\"last_failure\":\""
                + lastFailure.name().toLowerCase(Locale.ROOT)
                + "\"}";
    }

    synchronized long oldest() {
        return journal.oldest();
    }

    synchronized long latest() {
        return journal.latest();
    }

    synchronized List<CanObservationJournal.Record> history(long after, int limit) {
        return journal.after(after, limit);
    }

    /** Frames of the publications waiting for the link thread, in order. */
    synchronized List<byte[][]> pendingFrames() {
        List<byte[][]> frames = new java.util.ArrayList<>();

        for (Publication publication : pending) {
            frames.add(publication.frames);
        }

        return frames;
    }

    /** Frames last written for a slot. */
    synchronized byte[][] cached(String slot) {
        byte[][] frames = cache.get(slot);

        return frames == null ? null : Arrays.copyOf(frames, frames.length);
    }
}
