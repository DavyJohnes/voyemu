package com.qinggan.canbus.service;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;

public final class IpkFramesTest {
    private static void check(boolean condition) {
        if (!condition) {
            throw new AssertionError();
        }
    }

    private static void rejects(Runnable action) {
        try {
            action.run();
            throw new AssertionError("accepted invalid input");
        } catch (IllegalArgumentException expected) {
            // The encoder rejects the input.
        }
    }

    public static void main(String[] args) {
        checkEncodedFrames();
        checkRejectedInputs();
        checkFixedFrames();
        checkGuidance();
        checkMedia();
        checkRadio();
        checkReadiness();
        System.out.println("IpkFramesTest passed");
    }

    private static void checkEncodedFrames() {
        byte[][] incoming = IpkFrames.phone("COMING_CALL", 1, 0, "Иван Петров", "+79990000001");
        byte[] expected = new byte[77];

        expected[0] = 10;
        expected[1] = 16;
        expected[4] = 70;
        expected[5] = 106;
        expected[6] = 2;
        expected[7] = 1;
        expected[12] = 1;
        expected[13] = 21;
        expected[14] = 1;
        expected[76] = 13;
        byte[] russian = "Иван Петров".getBytes(StandardCharsets.UTF_8);
        check(russian.length == 21);
        System.arraycopy(russian, 0, expected, 15, russian.length);
        check(incoming.length == 1 && Arrays.equals(incoming[0], expected));
        byte[] outgoing = IpkFrames.phone("GOING_CALL", 0, 0, "", "+79990000001")[0];
        check(outgoing[6] == 1 && outgoing[7] == 2 && outgoing[12] == 2 && outgoing[13] == 12);
        check(new String(outgoing, 15, 12, StandardCharsets.UTF_8).equals("+79990000001"));
        byte[][] connected = IpkFrames.phone("CONNECTED", 1, 0x123456, null, "+7");
        check(
                connected.length == 2
                        && connected[1][6] == 4
                        && connected[1][12] == 3
                        && connected[1][13] == 3);
        check(
                (connected[1][15] & 255) == 0x12
                        && (connected[1][16] & 255) == 0x34
                        && (connected[1][17] & 255) == 0x56);
        check(connected[1][18] == 0 && connected[1][75] == 0 && connected[1][76] == 13);
        check(IpkFrames.phone("HANG_UP", 1, 0xffffff, null, null)[0][6] == 3);
        check(IpkFrames.phone("NOTHING", 0, 0, null, null).length == 1);
        check(IpkFrames.phone("HOLD", 1, 0, "held", null)[0][6] == 5);
        check(IpkFrames.phone("COMING_CALL", 0, 0, null, null).length == 0);
        String longAscii = new String(new char[70]).replace('\0', 'x');
        byte[] limited = IpkFrames.phone("COMING_CALL", 1, 0, longAscii, null)[0];
        check(limited[13] == 60 && limited[74] == 'x' && limited[75] == 0);

        for (int ascii : new int[] {57, 58, 59}) {
            String prefix = longAscii.substring(0, ascii);
            byte[] mixed = IpkFrames.phone("COMING_CALL", 1, 0, prefix + "Я", null)[0];
            int bytes = ascii == 59 ? 59 : ascii + 2;
            check(mixed[13] == bytes);
            check(
                    new String(mixed, 15, bytes, StandardCharsets.UTF_8)
                            .equals(prefix + (ascii == 59 ? "" : "Я")));
            check(mixed[15 + bytes] == 0);
        }

        byte[] astral =
                IpkFrames.phone(
                        "COMING_CALL", 1, 0, longAscii.substring(0, 58) + "\ud83d\ude00", null)[0];
        check(astral[13] == 58 && astral[73] == 0);
    }

    private static void checkRejectedInputs() {
        rejects(() -> IpkFrames.phone("unknown", 1, 0, "name", "+7"));
        rejects(() -> IpkFrames.phone("COMING_CALL", 2, 0, "name", "+7"));
        rejects(() -> IpkFrames.phone("COMING_CALL", 1, -1, "name", "+7"));
        rejects(() -> IpkFrames.phone("COMING_CALL", 1, 0x1000000, "name", "+7"));
        rejects(() -> IpkFrames.phone("COMING_CALL", 1, 0, "bad\0name", "+7"));
        rejects(() -> IpkFrames.phone("COMING_CALL", 1, 0, "bad\ud800", "+7"));
        rejects(() -> IpkFrames.phone("COMING_CALL", 1, 0, "bad\udc00", "+7"));
        rejects(() -> IpkFrames.phone("COMING_CALL", 1, 0, "valid", "bad\ud800x"));
    }

    private static byte[] expected(int length, int cmd, int... payload) {
        byte[] frame = new byte[length];

        frame[0] = 10;
        frame[1] = 16;
        frame[3] = (byte) ((length - 7) >>> 8);
        frame[4] = (byte) (length - 7);
        frame[5] = (byte) cmd;

        for (int i = 0; i < payload.length; i += 2) {
            frame[payload[i]] = (byte) payload[i + 1];
        }

        frame[length - 1] = 13;

        return frame;
    }

    private static String text(byte[] frame, int offset, int length) {
        return new String(frame, offset, length, StandardCharsets.UTF_8);
    }

    // Vectors follow the OEM InstrumentInteractionComponent and VoyahInstrumentClusterInfoSync.
    private static void checkFixedFrames() {
        check(Arrays.equals(IpkFrames.heartbeat(), new byte[] {10, 16, 0, 0, 2, 0x67, 1, 0, 13}));
        check(Arrays.equals(IpkFrames.guideOff(), new byte[] {10, 16, 0, 0, 2, 0x68, 2, 0, 13}));
        check(Arrays.equals(IpkFrames.roadOff(), new byte[] {10, 16, 0, 0, 2, 0x77, 2, 0, 13}));
        check(
                Arrays.equals(
                        IpkFrames.lanes(new int[0]), new byte[] {10, 16, 0, 0, 2, 0x76, 0, 0, 13}));
        check(
                Arrays.equals(
                        IpkFrames.lanes(new int[] {0x0102, 0x1ff}),
                        new byte[] {10, 16, 0, 0, 6, 0x76, 2, 1, 2, 1, (byte) 0xff, 0, 13}));
        check(
                Arrays.equals(
                        IpkFrames.cards(new int[] {7, 3, 4, 9, 10, 11}),
                        new byte[] {10, 16, 0, 0, 8, 0x75, 6, 7, 3, 4, 9, 10, 11, 0, 13}));
        check(
                Arrays.equals(
                        IpkFrames.cards(new int[0]), new byte[] {10, 16, 0, 0, 2, 0x75, 0, 0, 13}));
        check(IpkFrames.command(IpkFrames.heartbeat()) == 0x67);
        check(IpkFrames.command(new byte[] {10, 16, 0, 0, 3, 0x67, 1, 0, 13}) == -1);
        check(IpkFrames.command(new byte[] {10, 16, 0, 0, 2, 0x67, 1, 0, 12}) == -1);
        check(IpkFrames.command(null) == -1);
    }

    private static void checkGuidance() {
        byte[] guide = IpkFrames.guide(0x0105, "1.5", "公里", "Тверская");
        int d = 3, u = "公里".getBytes(StandardCharsets.UTF_8).length;
        int r = "Тверская".getBytes(StandardCharsets.UTF_8).length;
        byte[] want = expected(17 + d + u + r, 0x68, 6, 1, 7, 1, 8, 5, 11, d, 12, u, 13, r);

        System.arraycopy("1.5".getBytes(StandardCharsets.UTF_8), 0, want, 15, d);
        System.arraycopy("公里".getBytes(StandardCharsets.UTF_8), 0, want, 15 + d, u);
        System.arraycopy("Тверская".getBytes(StandardCharsets.UTF_8), 0, want, 15 + d + u, r);
        check(Arrays.equals(guide, want));
        byte[] road = IpkFrames.road("Ленинский проспект");
        int n = "Ленинский проспект".getBytes(StandardCharsets.UTF_8).length;
        check(road.length == n + 17 && road[4] == n + 10 && road[5] == 0x77 && road[6] == 1);
        check(road[13] == n && text(road, 15, n).equals("Ленинский проспект"));
        check(IpkFrames.road("") == null && IpkFrames.road(null) == null);
        check(IpkInfoSync.distance(4).equals("0"));
        check(IpkInfoSync.distance(14).equals("10") && IpkInfoSync.distance(15).equals("20"));
        check(IpkInfoSync.distance(549).equals("500") && IpkInfoSync.distance(1260).equals("1.2"));
        check(IpkInfoSync.distance(1275).equals("1.2") && IpkInfoSync.distance(1300).equals("1.3"));
        check(IpkInfoSync.distance(123456).equals("123"));
        check(IpkInfoSync.unit(999, java.util.Locale.CHINA).equals("米"));
        check(IpkInfoSync.unit(1000, java.util.Locale.US).equals("km"));
        check(IpkInfoSync.unit(1000, java.util.Locale.forLanguageTag("ru")).equals("公里"));
    }

    private static void checkMedia() {
        byte[][] media = IpkFrames.media(2, 2, 3, "Песня", "Исполнитель");
        int song = "Песня".getBytes(StandardCharsets.UTF_8).length;
        int artist = "Исполнитель".getBytes(StandardCharsets.UTF_8).length;
        byte[] first = expected(77, 0x6e, 6, 2, 7, 2, 8, 3, 12, 1, 13, song, 14, 1);

        System.arraycopy("Песня".getBytes(StandardCharsets.UTF_8), 0, first, 15, song);
        check(media.length == 2 && Arrays.equals(media[0], first));
        check(media[1][12] == 2 && media[1][13] == artist && media[1][14] == 1);
        check(text(media[1], 15, artist).equals("Исполнитель") && media[1][15 + artist] == 0);
        byte[][] unknown = IpkFrames.media(3, 6, 1, null, null);
        check(unknown.length == 1 && unknown[0][12] == 2 && unknown[0][13] == 1);
        check(unknown[0][15] == ' ' && unknown[0][6] == 3 && unknown[0][7] == 6);
        String long100 = new String(new char[100]).replace('\0', 'a');
        byte[][] split = IpkFrames.media(1, 0, 3, long100 + "bbbbbbbbbbbbbbbbbbbbbbbbb", "x");
        check(split.length == 3 && split[0][13] == 120 && split[1][13] == 120);
        check(split[0][14] == 1 && split[1][14] == 2 && split[0][74] == 'a');
        check(text(split[1], 15, 60).equals(long100.substring(60) + "bbbbbbbbbbbbbbbbbbbb"));
        check(split[1][75] == 0 && split[2][12] == 2);
    }

    private static void checkRadio() {
        byte[][] radio = IpkFrames.radio(1, 3, 2, "101.7", null);

        check(radio.length == 1 && radio[0][5] == 0x6f && radio[0][6] == 1 && radio[0][7] == 3);
        check(radio[0][12] == 2 && radio[0][13] == 5 && text(radio[0], 15, 5).equals("101.7"));
        String long150 = new String(new char[150]).replace('\0', 'c');
        byte[][] network = IpkFrames.radio(3, 3, 5, long150, "Альбом");
        check(network.length == 4 && network[2][14] == 3 && (network[2][13] & 255) == 150);
        check(network[3][12] == 1 && network[3][6] == 3 && network[3][14] == 1);
        byte[][] phone = IpkFrames.phone("CONNECTED", 0, 5, "Иван", null);
        check(phone.length == 2 && phone[0][5] == 0x6a && phone[1][14] == 1 && phone[1][17] == 5);
    }

    private static void checkReadiness() {
        check(Arrays.equals(IpkFrames.ready("qg.car.cluster.NAVI", 1), expected(20, 0x60, 7, 2)));
        check(Arrays.equals(IpkFrames.ready("qg.car.cluster.NAVI", 2), expected(20, 0x60, 7, 1)));
        check(Arrays.equals(IpkFrames.ready("qg.car.cluster.MEDIA", 2), expected(20, 0x61, 10, 3)));
        check(Arrays.equals(IpkFrames.ready("qg.car.cluster.PHONE", 1), expected(20, 0x63, 10, 2)));
        check(Arrays.equals(IpkFrames.ready("qg.car.cluster.REST", 1), expected(20, 0x7a, 6, 1)));
        check(Arrays.equals(IpkFrames.ready("qg.car.cluster.CAMP", 0), expected(20, 0x7d)));
        check(IpkFrames.ready("qg.car.cluster.OTHER", 1) == null);
        check(
                Arrays.equals(
                        IpkFrames.presentation("qg.car.cluster.NAVI_FULL", 1),
                        expected(20, 0x60, 6, 1, 7, 2, 8, 4)));
        check(
                Arrays.equals(
                        IpkFrames.presentation("qg.car.cluster.MEDIA_SMALL", 2),
                        expected(20, 0x61, 6, 2)));
        check(
                Arrays.equals(
                        IpkFrames.presentation("qg.car.cluster.CAMP", 1),
                        expected(20, 0x7e, 6, 1)));
        check(IpkFrames.presentation("qg.car.cluster.NAVI", 1) == null);
        check(
                Arrays.equals(
                        IpkFrames.readyToShow("qg.car.cluster.NVS", 7),
                        expected(20, 0x60, 2, 7, 8, 2, 11, 1)));
        check(
                Arrays.equals(
                        IpkFrames.readyToShow("qg.car.cluster.FAVORITE", 3),
                        expected(20, 0x60, 2, 3, 9, 1, 11, 2)));
        check(
                Arrays.equals(
                        IpkFrames.readyToShow("qg.car.cluster.CONTACTS", 3),
                        expected(20, 0x63, 11, 1)));
        check(Arrays.equals(IpkFrames.readyToShow("qg.car.cluster.REST", 0), expected(20, 0x7c)));
        check(IpkFrames.readyToShow("qg.car.cluster.PHONE", 0) == null);
    }
}
