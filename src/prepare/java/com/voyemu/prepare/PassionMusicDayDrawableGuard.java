package com.voyemu.prepare;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.HexFormat;
import java.util.Map;

/** Supply only three absent daytime entries using their existing exact OEM assets. */
final class PassionMusicDayDrawableGuard {
    static final String NAME = "passion-api30-music-day-drawables";
    static final String PIN = "a3c788860d5df27277d53ffbf838daaefddb6ab098ad52b636671dd70b34765d";
    static final int PACKAGE = 105636,
            DAY = 479796,
            NIGHT = 488844,
            HEADER = 84,
            DATA = 1812,
            DAY_SIZE = 7220;
    static final int[] IDS = {0x113, 0x118, 0x131};
    static final String[] NAMES = {"ic_pause_normal", "ic_play_normal", "ic_thumb"};
    static final String[] ENTRIES = {
        "08000000870f0000080000036e010000",
        "080000008c0f00000800000371010000",
        "08000000a50f0000080000037e010000"
    };

    private static void require(boolean ok) {
        Failure.require(
                ok,
                "PASSION_MUSIC_DAY_DRAWABLE_CONTRACT",
                "Exact original Passion Music three drawable fallback required",
                3);
    }

    private static int u16(byte[] b, int at) {
        require(at >= 0 && at + 2 <= b.length);

        return Short.toUnsignedInt(
                ByteBuffer.wrap(b, at, 2).order(ByteOrder.LITTLE_ENDIAN).getShort());
    }

    private static int word(byte[] b, int at) {
        require(at >= 0 && at + 4 <= b.length);

        return ByteBuffer.wrap(b, at, 4).order(ByteOrder.LITTLE_ENDIAN).getInt();
    }

    private static void put(byte[] b, int at, int value) {
        ByteBuffer.wrap(b, at, 4).order(ByteOrder.LITTLE_ENDIAN).putInt(value);
    }

    private static byte[] entry(byte[] table, int id) {
        int offset = word(table, NIGHT + HEADER + 4 * id);

        require(offset >= 0 && offset + 16 <= word(table, NIGHT + 4) - DATA);

        return Arrays.copyOfRange(table, NIGHT + DATA + offset, NIGHT + DATA + offset + 16);
    }

    static void validate(String profile, int api, Path original, Map<String, byte[]> entries)
            throws Exception {
        require(api == 30 && FilesSupport.hash(original).equals(PIN));
        byte[] table = entries.get("resources.arsc");
        require(table != null && table.length == 706764);
        requireTableLayout(table);

        for (int i = 0; i < IDS.length; i++) {
            require(
                    word(table, DAY + HEADER + 4 * IDS[i]) == -1
                            && HexFormat.of().formatHex(entry(table, IDS[i])).equals(ENTRIES[i]));
            byte[] asset = entries.get("res/drawable-night-v8/" + NAMES[i] + ".webp");
            require(asset != null);
        }

        require(
                entries.containsKey("res/drawable/selector_play.xml")
                        && entries.containsKey("res/drawable/selector_pause.xml")
                        && entries.containsKey("res/drawable/selector_seekbar_thumb.xml"));
    }

    private static void requireTableLayout(byte[] table) {
        require(
                u16(table, 0) == 2
                        && u16(table, 2) == 12
                        && word(table, 4) == table.length
                        && word(table, 8) == 1);
        require(
                u16(table, PACKAGE) == 0x200
                        && u16(table, PACKAGE + 2) == 288
                        && word(table, PACKAGE + 4) == 601128
                        && word(table, PACKAGE + 8) == 0x7f);

        for (int start : new int[] {DAY, NIGHT}) {
            require(
                    u16(table, start) == 0x201
                            && u16(table, start + 2) == HEADER
                            && word(table, start + 8) == 7
                            && word(table, start + 12) == 432
                            && word(table, start + 16) == DATA
                            && word(table, start + 20) == 64);

            for (int i = 4; i < 64; i++) {
                require(table[start + 20 + i] == (byte) (start == NIGHT && i == 29 ? 32 : 0));
            }
        }

        require(word(table, DAY + 4) == DAY_SIZE && word(table, NIGHT + 4) == 3044);
    }

    static byte[] apply(String profile, int api, Path original, Map<String, byte[]> entries)
            throws Exception {
        validate(profile, api, original, entries);
        byte[] before = entries.get("resources.arsc");
        int at = DAY + DAY_SIZE;
        byte[] out = new byte[before.length + 48];
        System.arraycopy(before, 0, out, 0, at);
        System.arraycopy(before, at, out, at + 48, before.length - at);

        for (int i = 0; i < IDS.length; i++) {
            System.arraycopy(entry(before, IDS[i]), 0, out, at + 16 * i, 16);
            put(out, DAY + HEADER + 4 * IDS[i], DAY_SIZE - DATA + 16 * i);
        }

        for (int offset : new int[] {4, PACKAGE + 4, DAY + 4}) {
            put(out, offset, word(before, offset) + 48);
        }

        // Exact inverse proves every other byte, config, string pool and entry remains unchanged.
        byte[] inverse = new byte[before.length];
        System.arraycopy(out, 0, inverse, 0, at);
        System.arraycopy(out, at + 48, inverse, at, before.length - at);

        for (int offset : new int[] {4, PACKAGE + 4, DAY + 4}) {
            put(inverse, offset, word(out, offset) - 48);
        }

        for (int id : IDS) {
            put(inverse, DAY + HEADER + 4 * id, -1);
        }

        require(Arrays.equals(before, inverse));

        return out;
    }
}
