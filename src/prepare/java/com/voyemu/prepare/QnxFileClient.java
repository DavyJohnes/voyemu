package com.voyemu.prepare;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.List;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.regex.Pattern;

/** Read-only qconn file transport. Binary bodies must never undergo telnet decoding. */
final class QnxFileClient implements AutoCloseable {
    static final class Wire {
        final InputStream input;
        final OutputStream output;

        Wire(InputStream input, OutputStream output) {
            this.input = new BufferedInputStream(input, 65536);
            this.output = output;
        }

        int raw() throws Exception {
            int value = input.read();

            Failure.require(value >= 0, "QNX_EOF", "QNX closed the bounded read stream", 5);

            return value;
        }

        int text() throws Exception {
            while (true) {
                int value = raw();

                if (value != 255) {
                    return value;
                }

                int verb = raw();

                if (verb == 255) {
                    return 255;
                }

                if (verb >= 251 && verb <= 254) {
                    int option = raw();

                    if (verb == 251 || verb == 253) {
                        send(
                                new byte[] {
                                    (byte) 255, (byte) (verb == 251 ? 254 : 252), (byte) option
                                });
                    }
                } else if (verb == 250) {
                    int previous = 0;

                    while (true) {
                        int current = raw();

                        if (previous == 255 && current == 240) {
                            break;
                        }

                        previous = current;
                    }
                }
            }
        }

        byte[] until(String suffix) throws Exception {
            byte[] end = suffix.getBytes(StandardCharsets.US_ASCII);
            ByteArrayOutputStream value = new ByteArrayOutputStream();
            int matched = 0;

            while (matched < end.length) {
                int next = text();
                value.write(next);
                Failure.require(
                        value.size() <= 65536,
                        "QNX_PROTOCOL_LIMIT",
                        "QNX response exceeds bound",
                        5);
                matched =
                        next == (end[matched] & 255) ? matched + 1 : next == (end[0] & 255) ? 1 : 0;
            }

            return value.toByteArray();
        }

        void send(byte[] bytes) throws Exception {
            output.write(bytes);
            output.flush();
        }

        void send(String text) throws Exception {
            send(text.getBytes(StandardCharsets.US_ASCII));
        }

        byte[] body(int size) throws Exception {
            Failure.require(
                    size >= 0 && size <= 65536, "QNX_BODY_LIMIT", "Bounded QNX body required", 5);
            byte[] result = input.readNBytes(size);
            Failure.require(
                    result.length == size, "QNX_TRUNCATED", "Incomplete QNX binary body", 5);

            return result;
        }
    }

    record Open(long fd, long bytes) {}

    final Process process;
    final ScheduledExecutorService timer;
    final ScheduledFuture<?> deadline;
    final Wire wire;

    QnxFileClient(Path adb, String serial, String host, int port, int seconds) throws Exception {
        Failure.require(
                adb.isAbsolute()
                        && serial.matches("[A-Za-z0-9][A-Za-z0-9_.:-]*")
                        && !serial.startsWith("emulator-"),
                "QNX_SERIAL",
                "Explicit physical serial required",
                2);
        Failure.require(
                host.matches("(?:[0-9]{1,3}\\.){3}[0-9]{1,3}")
                        && port > 0
                        && port <= 65535
                        && seconds > 0
                        && seconds <= 900,
                "QNX_ENDPOINT",
                "Explicit bounded qconn endpoint required",
                2);

        for (String octet : host.split("\\.", -1)) {
            Failure.require(
                    Integer.parseInt(octet) <= 255, "QNX_ENDPOINT", "Invalid IPv4 endpoint", 2);
        }

        process =
                new ProcessBuilder(
                                List.of(
                                        adb.toString(),
                                        "-s",
                                        serial,
                                        "shell",
                                        "-T",
                                        "/sbin/busybox",
                                        "timeout",
                                        "-t",
                                        Integer.toString(seconds + 2),
                                        "/sbin/busybox",
                                        "nc",
                                        "-w",
                                        "1",
                                        host,
                                        Integer.toString(port)))
                        .redirectError(ProcessBuilder.Redirect.INHERIT)
                        .start();
        timer =
                Executors.newSingleThreadScheduledExecutor(task -> {
                    Thread thread = new Thread(task, "qnx-read-deadline");
                    thread.setDaemon(true);

                    return thread;
                });
        deadline = timer.schedule(() -> process.destroyForcibly(), seconds, TimeUnit.SECONDS);
        wire = new Wire(process.getInputStream(), process.getOutputStream());
    }

    void begin() throws Exception {
        wire.until("QCONN\r\n");
        wire.send("service file\n");
        String reply = new String(wire.until("<qconn-file> "), StandardCharsets.US_ASCII);
        Failure.require(
                reply.contains("OK\r\n"),
                "QNX_SERVICE",
                "Read-only QNX file service unavailable",
                5);
    }

    static long extent(long mode, long units) {
        long kind = mode & 0170000;

        Failure.require(
                units > 0 && (kind == 0060000 || kind == 0100000),
                "QNX_SOURCE_KIND",
                "QNX source must be a block device or regular file",
                5);
        Failure.require(
                units <= ((4L << 30) / (kind == 0060000 ? 512 : 1)),
                "QNX_SOURCE_LIMIT",
                "QNX source exceeds 4GiB bound",
                5);

        return units * (kind == 0060000 ? 512 : 1);
    }

    Open open(String path) throws Exception {
        Failure.require(
                path.matches("/(?:bin|dev/disk|images)/[A-Za-z0-9_.-]+"),
                "QNX_SOURCE_PATH",
                "Explicit QNX system source required",
                2);
        wire.send("o:\"" + path + "\":0\n");
        String reply = new String(wire.until("<qconn-file> "), StandardCharsets.US_ASCII);
        var match =
                Pattern.compile("o:([0-9a-f]+):([0-9a-f]+):([0-9a-f]+):\"([^\"\\r\\n]+)\"")
                        .matcher(reply);
        Failure.require(
                match.find() && match.group(4).equals(path),
                "QNX_SOURCE_OPEN",
                "Read-only QNX source open failed",
                5);

        return new Open(
                Long.parseUnsignedLong(match.group(1), 16),
                extent(
                        Long.parseUnsignedLong(match.group(2), 16),
                        Long.parseUnsignedLong(match.group(3), 16)));
    }

    static byte[] read(Wire wire, long fd, long offset, int count) throws Exception {
        Failure.require(
                fd >= 0 && offset >= 0 && count > 0 && count <= 65536,
                "QNX_READ_LIMIT",
                "Bounded QNX read required",
                2);
        wire.send(
                "r:"
                        + Long.toHexString(fd)
                        + ":"
                        + Long.toHexString(offset)
                        + ":"
                        + Integer.toHexString(count)
                        + "\n");
        ByteArrayOutputStream result = new ByteArrayOutputStream();

        while (true) {
            String line = new String(wire.until("\n"), StandardCharsets.US_ASCII);
            var continuation = Pattern.compile("o\\+:([0-9a-f]+)\\r?\\n").matcher(line);
            var terminal =
                    Pattern.compile("o:([0-9a-f]+):[0-9a-f]+:[0-9a-f]+\\r?\\n").matcher(line);
            boolean more = continuation.matches();
            Failure.require(
                    more || terminal.matches(), "QNX_READ_REPLY", "Invalid QNX read framing", 5);
            long length = Long.parseUnsignedLong((more ? continuation : terminal).group(1), 16);
            Failure.require(
                    length >= 0 && length <= count - result.size() && (!more || length > 0),
                    "QNX_FRAGMENT_LIMIT",
                    "Invalid QNX fragment extent",
                    5);
            result.write(wire.body((int) length));

            if (!more) {
                break;
            }
        }

        wire.until("<qconn-file> ");

        return result.toByteArray();
    }

    byte[] read(long fd, long offset, int count) throws Exception {
        return read(wire, fd, offset, count);
    }

    void closeFile(long fd) throws Exception {
        wire.send("c:" + Long.toHexString(fd) + "\n");
        String reply = new String(wire.until("<qconn-file> "), StandardCharsets.US_ASCII);
        Failure.require(
                Pattern.compile("(?:^|\\n)o\\s*\\r?\\n").matcher(reply).find(),
                "QNX_SOURCE_CLOSE",
                "QNX source close failed",
                5);
    }

    @Override
    public void close() throws java.io.IOException {
        deadline.cancel(false);
        timer.shutdownNow();
        process.destroy();

        try {
            if (!process.waitFor(2, TimeUnit.SECONDS)) {
                process.destroyForcibly();
            }
        } catch (InterruptedException error) {
            process.destroyForcibly();
            Thread.currentThread().interrupt();
        }

        process.getInputStream().close();
        process.getOutputStream().close();
    }
}
