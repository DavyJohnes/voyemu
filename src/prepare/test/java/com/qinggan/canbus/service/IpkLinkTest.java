package com.qinggan.canbus.service;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.NoRouteToHostException;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketException;
import java.net.SocketTimeoutException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.BooleanSupplier;

/**
 * The IPK link against a fake ivcd: the persistent connection, heartbeat supervision and
 * reconnection, cluster requests answered from the last frames, and no repeated write.
 */
public final class IpkLinkTest {
    private static void check(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    private static void await(BooleanSupplier condition, String message)
            throws InterruptedException {
        long deadline = System.nanoTime() + 5_000_000_000L;

        while (!condition.getAsBoolean()) {
            if (System.nanoTime() > deadline) {
                throw new AssertionError(message);
            }

            Thread.sleep(10);
        }
    }

    /** One accepted connection of the fake ivcd with the frames it read. */
    static final class Connection {
        final Socket socket;
        final List<byte[]> frames = new CopyOnWriteArrayList<>();

        Connection(Socket socket) {
            this.socket = socket;
        }

        List<byte[]> data() {
            List<byte[]> result = new ArrayList<>();

            for (byte[] frame : frames) {
                if (frame[5] != 0x67) {
                    result.add(frame);
                }
            }

            return result;
        }

        long heartbeats() {
            return frames.stream().filter(frame -> frame[5] == 0x67).count();
        }

        void write(byte[] bytes) throws IOException {
            OutputStream output = socket.getOutputStream();

            output.write(bytes);
            output.flush();
        }
    }

    /** Fake ivcd: accepts connections, reads IVC frames and answers heartbeats while replying. */
    static final class Ivcd implements AutoCloseable {
        final ServerSocket server = new ServerSocket();
        final List<Connection> connections = new CopyOnWriteArrayList<>();
        volatile boolean reply = true;

        Ivcd() throws IOException {
            server.bind(new InetSocketAddress(InetAddress.getLoopbackAddress(), 0));
            Thread accept = new Thread(this::accept, "FakeIvcd");
            accept.setDaemon(true);
            accept.start();
        }

        InetSocketAddress address() {
            return new InetSocketAddress(InetAddress.getLoopbackAddress(), server.getLocalPort());
        }

        Connection last() {
            return connections.get(connections.size() - 1);
        }

        private void accept() {
            try {
                for (; ; ) {
                    Connection connection = new Connection(server.accept());
                    connections.add(connection);
                    Thread read = new Thread(() -> read(connection), "FakeIvcdRead");
                    read.setDaemon(true);
                    read.start();
                }
            } catch (IOException closed) {
                // The test closed the server.
            }
        }

        private static byte[] exactly(InputStream input, int count) throws IOException {
            byte[] bytes = input.readNBytes(count);

            if (bytes.length != count) {
                throw new IOException("eof");
            }

            return bytes;
        }

        private void read(Connection connection) {
            try (Socket socket = connection.socket) {
                InputStream input = socket.getInputStream();

                for (; ; ) {
                    byte[] head = exactly(input, 5);
                    int length = ((head[3] & 255) << 8) | (head[4] & 255);
                    byte[] frame = Arrays.copyOf(head, length + 7);
                    System.arraycopy(exactly(input, length + 2), 0, frame, 5, length + 2);
                    connection.frames.add(frame);

                    if (frame[5] == 0x67 && reply) {
                        connection.write(reply(0x87, 0));
                    }
                }
            } catch (IOException closed) {
                // Link or test closed the connection.
            }
        }

        @Override
        public void close() throws IOException {
            server.close();

            for (Connection connection : connections) {
                connection.socket.close();
            }
        }
    }

    /** Cluster frame 0A 20 seq 00 0D cmd value ... 00 0D of 20 bytes. */
    static byte[] reply(int cmd, int value) {
        byte[] frame = new byte[20];

        frame[0] = 10;
        frame[1] = 32;
        frame[2] = 5;
        frame[4] = 13;
        frame[5] = (byte) cmd;
        frame[6] = (byte) value;
        frame[19] = 13;

        return frame;
    }

    private static IpkLink link(Ivcd ivcd) {
        return new IpkLink(ivcd.address(), 100, 400, 50, 200);
    }

    private static List<String> history(IpkLink link) {
        List<String> events = new ArrayList<>();

        for (CanObservationJournal.Record record : link.history(0, 512)) {
            events.add(record.json);
        }

        return events;
    }

    static void classification() {
        check(
                IpkLink.reason(new SocketTimeoutException()) == IpkLink.Reason.TIMEOUT
                        && IpkLink.ioClass(new SocketTimeoutException()) == IpkLink.IoClass.TIMEOUT,
                "timeout");
        check(
                IpkLink.reason(new NoRouteToHostException()) == IpkLink.Reason.ROUTE
                        && IpkLink.ioClass(new NoRouteToHostException())
                                == IpkLink.IoClass.NO_ROUTE,
                "route");

        int[] errnos = {111, 13, 101, 104, 110};
        IpkLink.Reason[] reasons = {
            IpkLink.Reason.REFUSED,
            IpkLink.Reason.PERMISSION,
            IpkLink.Reason.ROUTE,
            IpkLink.Reason.CLOSED,
            IpkLink.Reason.TIMEOUT
        };

        for (int i = 0; i < errnos.length; i++) {
            SocketException io = new SocketException("private-number-must-not-leak");
            io.initCause(new android.system.ErrnoException(errnos[i]));
            check(
                    IpkLink.reason(io) == reasons[i]
                            && IpkLink.ioClass(io) == IpkLink.IoClass.SOCKET,
                    "errno " + errnos[i]);
        }

        check(
                IpkLink.reason(new java.net.ConnectException("permission denied"))
                        == IpkLink.Reason.OTHER_IO,
                "message text must not classify");
        check(
                IpkLink.ioClass(new java.nio.channels.ClosedByInterruptException())
                        == IpkLink.IoClass.CLOSED_CHANNEL,
                "closed channel");
    }

    static void heartbeatAndCache() throws Exception {
        try (Ivcd ivcd = new Ivcd()) {
            IpkLink link = link(ivcd);
            List<byte[]> heard = Collections.synchronizedList(new ArrayList<>());
            link.listen(heard::add);
            link.start();
            await(() -> !ivcd.connections.isEmpty() && ivcd.last().heartbeats() >= 3, "heartbeats");
            await(() -> link.status().contains("\"state\":\"connected\""), "connected");
            check(link.status().contains("\"generation\":1"), "one connection: " + link.status());
            byte[][] media = IpkFrames.media(2, 2, 3, "Song", "Artist");
            AtomicInteger receipts = new AtomicInteger();
            long sequence =
                    link.send(
                            "media",
                            "media",
                            "test",
                            media,
                            (seq, submitted, stage, reason, ioClass) -> {
                                check(
                                        submitted
                                                && stage == IpkLink.Stage.COMPLETE
                                                && reason == IpkLink.Reason.NONE,
                                        "submitted receipt");
                                receipts.incrementAndGet();
                            });
            check(sequence == 1, "first sequence");
            await(() -> ivcd.last().data().size() == 2 && receipts.get() == 1, "media written");
            check(Arrays.equals(ivcd.last().data().get(0), media[0]), "media bytes");
            ivcd.last().write(new byte[] {1, 2, 3});
            ivcd.last().write(reply(0x81, 1));
            await(() -> ivcd.last().data().size() == 4, "media request answered from cache");
            check(Arrays.equals(ivcd.last().data().get(3), media[1]), "cached artist frame");
            await(() -> heard.size() == 1 && heard.get(0)[5] == (byte) 0x81, "listener frame");
            ivcd.last().write(reply(0x81, 2));
            ivcd.last().write(reply(0x83, 1));
            Thread.sleep(300);
            check(ivcd.last().data().size() == 4, "close and an empty slot send nothing");
            check(link.status().contains("\"malformed\":3"), "resync: " + link.status());
            List<String> events = history(link);
            check(
                    events.stream().anyMatch(e -> e.contains("\"origin\":\"request\""))
                            && events.stream()
                                    .anyMatch(e ->
                                            e.contains("\"direction\":\"received\"")
                                                    && e.contains("\"cmd\":\"0x81\""))
                            && events.stream().noneMatch(e -> e.contains("Song")),
                    "history without text: " + events);
            link.stop();
        }
    }

    static void supervisionWithoutReplay() throws Exception {
        try (Ivcd ivcd = new Ivcd()) {
            IpkLink link = link(ivcd);
            link.start();
            await(() -> link.status().contains("\"state\":\"connected\""), "connected");
            link.send(
                    "phone",
                    "phone",
                    "test",
                    IpkFrames.phone("COMING_CALL", 1, 0, "A", null),
                    null);
            await(() -> ivcd.last().data().size() == 1, "phone written once");
            ivcd.reply = false;
            await(() -> ivcd.connections.size() == 2, "supervision reconnects");
            check(
                    link.status().contains("\"last_failure\":\"supervision\""),
                    "supervision reason: " + link.status());
            ivcd.reply = true;
            await(() -> link.status().contains("\"generation\":2"), "second generation");
            Thread.sleep(300);
            check(ivcd.last().data().isEmpty(), "no publication is replayed after reconnection");
            check(ivcd.connections.get(0).data().size() == 1, "the phone frame was written once");
            ivcd.last().socket.close();
            await(() -> ivcd.connections.size() == 3, "peer close reconnects");
            link.stop();
        }
    }

    static void failedWriteNotRepeated() throws Exception {
        Ivcd ivcd = new Ivcd();
        InetSocketAddress address = ivcd.address();

        ivcd.close();
        IpkLink link = new IpkLink(address, 100, 400, 50, 200);
        AtomicInteger failed = new AtomicInteger();
        link.start();
        link.send(
                "phone",
                "phone",
                "test",
                IpkFrames.phone("COMING_CALL", 1, 0, "B", null),
                (seq, submitted, stage, reason, ioClass) -> {
                    check(!submitted && stage == IpkLink.Stage.CONNECT, "not connected");
                    failed.incrementAndGet();
                });
        await(() -> failed.get() == 1, "failure receipt");
        check(link.cached("phone") == null, "a failed write is not cached");
        check(
                history(link).stream().anyMatch(e -> e.contains("\"outcome\":\"failed\"")),
                "failed outcome journaled");
        link.stop();
    }

    static void boundedQueue() {
        IpkLink link = new IpkLink(new InetSocketAddress(InetAddress.getLoopbackAddress(), 9));

        for (int i = 0; i < 64; i++) {
            link.send("raw", null, "test", new byte[][] {IpkFrames.heartbeat()}, null);
        }

        try {
            link.send("raw", null, "test", new byte[][] {IpkFrames.heartbeat()}, null);
            throw new AssertionError("full queue accepted");
        } catch (IllegalStateException expected) {
            // The 65th publication is rejected and journaled as dropped.
        }

        check(link.pendingFrames().size() == 64, "pending publications");
        check(
                history(link).size() == 1 && history(link).get(0).contains("\"dropped\""),
                "dropped publication journaled");

        try {
            link.send("raw", null, "test", new byte[][] {{1, 2, 3}}, null);
            throw new AssertionError("invalid frame accepted");
        } catch (IllegalArgumentException expected) {
            // Only complete IVC frames are written.
        }

        check(link.send("raw", null, "test", new byte[0][], null) == 0, "no frames");
    }

    public static void main(String[] args) throws Exception {
        classification();
        boundedQueue();
        heartbeatAndCache();
        supervisionWithoutReplay();
        failedWriteNotRepeated();
        System.out.println("IpkLinkTest PASS");
    }
}
