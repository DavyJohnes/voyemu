package com.voyemu.prepare;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

public final class QnxFileClientTest {
    static void check(boolean value, String reason) {
        if (!value) {
            throw new AssertionError(reason);
        }
    }

    interface Probe {
        void run() throws Exception;
    }

    static void rejected(Probe probe) throws Exception {
        boolean failed = false;

        try {
            probe.run();
        } catch (Failure error) {
            failed = true;
        }

        check(failed, "invalid QNX stream accepted");
    }

    static byte[] framed(byte[] body) throws Exception {
        ByteArrayOutputStream out = new ByteArrayOutputStream();

        out.write("o+:2\r\n".getBytes(StandardCharsets.US_ASCII));
        out.write(body, 0, 2);
        out.write("o:2:0:0\r\n".getBytes(StandardCharsets.US_ASCII));
        out.write(body, 2, 2);
        out.write("<qconn-file> ".getBytes(StandardCharsets.US_ASCII));

        return out.toByteArray();
    }

    public static void main(String[] args) throws Exception {
        byte[] body = new byte[] {(byte) 255, 0, 13, 10};
        ByteArrayOutputStream commands = new ByteArrayOutputStream();
        QnxFileClient.Wire wire =
                new QnxFileClient.Wire(new ByteArrayInputStream(framed(body)), commands);

        check(
                Arrays.equals(QnxFileClient.read(wire, 3, 16, 4), body),
                "binary telnet/newline bytes changed");
        check(
                commands.toString(StandardCharsets.US_ASCII).equals("r:3:10:4\n"),
                "read command is not exact");
        ByteArrayOutputStream reply = new ByteArrayOutputStream();
        wire =
                new QnxFileClient.Wire(
                        new ByteArrayInputStream(
                                new byte[] {(byte) 255, (byte) 251, 1, 'O', 'K', '\r', '\n'}),
                        reply);
        check(
                new String(wire.until("\n"), StandardCharsets.US_ASCII).equals("OK\r\n"),
                "telnet negotiation entered text");
        check(
                Arrays.equals(reply.toByteArray(), new byte[] {(byte) 255, (byte) 254, 1}),
                "telnet refusal changed");
        rejected(() ->
                QnxFileClient.read(
                        new QnxFileClient.Wire(
                                new ByteArrayInputStream(
                                        "o+:0\n".getBytes(StandardCharsets.US_ASCII)),
                                new ByteArrayOutputStream()),
                        1,
                        0,
                        1));
        rejected(() ->
                QnxFileClient.read(
                        new QnxFileClient.Wire(
                                new ByteArrayInputStream(
                                        "o:2:0:0\nX".getBytes(StandardCharsets.US_ASCII)),
                                new ByteArrayOutputStream()),
                        1,
                        0,
                        2));
        rejected(() ->
                QnxFileClient.read(
                        new QnxFileClient.Wire(
                                new ByteArrayInputStream(
                                        "o:3:0:0\nXXX".getBytes(StandardCharsets.US_ASCII)),
                                new ByteArrayOutputStream()),
                        1,
                        0,
                        2));
        check(QnxFileClient.extent(0060000, 2048) == 1048576, "block units were not converted");
        check(QnxFileClient.extent(0100000, 123) == 123, "regular file units changed");
        rejected(() -> QnxFileClient.extent(0040000, 123));
        rejected(() -> QnxFileClient.extent(0060000, Long.MAX_VALUE));
        System.out.println(
                "QNX binary continuation, telnet, truncation, extent and read-only framing tests"
                        + " passed");
    }
}
