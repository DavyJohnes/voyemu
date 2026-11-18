package com.voyemu.prepare;

import java.util.Arrays;

/** Tool-only Android 9 hidden-API decoding; original VDEX bytes stay untouched. */
final class BootVdex {
    private static void check(boolean condition) {
        if (!condition) {
            throw new IllegalArgumentException("FRAMEWORK_VDEX_INVALID");
        }
    }

    private static long u32(byte[] bytes, long offset) {
        check(offset >= 0 && offset + 4 <= bytes.length);
        int at = (int) offset;

        return (bytes[at] & 255L)
                | ((bytes[at + 1] & 255L) << 8)
                | ((bytes[at + 2] & 255L) << 16)
                | ((bytes[at + 3] & 255L) << 24);
    }

    private static boolean magic(byte[] data, long offset, String value) {
        if (offset < 0 || offset + value.length() > data.length) {
            return false;
        }

        for (int i = 0; i < value.length(); i++) {
            if ((data[(int) offset + i] & 255) != value.charAt(i)) {
                return false;
            }
        }

        return true;
    }

    private static final class Leb {
        final byte[] bytes;
        final long start;
        final long end;
        long cursor;

        Leb(byte[] bytes, long start, long end, long cursor) {
            this.bytes = bytes;
            this.start = start;
            this.end = end;
            this.cursor = cursor;
        }

        long read() {
            long result = 0;

            for (int i = 0; i < 5; i++) {
                check(cursor >= start && cursor < end);
                int current = bytes[(int) cursor++] & 255;
                check(i < 4 || current < 16);
                result |= (long) (current & 127) << (i * 7);

                if ((current & 128) == 0) {
                    return result;
                }
            }

            throw new IllegalArgumentException("FRAMEWORK_VDEX_INVALID");
        }
    }

    static byte[] decode(byte[] input) {
        check(input.length >= 36 && magic(input, 0, "vdex019\0" + "002\0"));
        byte[] output = Arrays.copyOf(input, input.length);
        long count = u32(output, 12);
        check(count > 0 && 20 + count * 4 + 12 <= output.length);
        long section = 20 + count * 4;
        long dexEnd = section + 12 + u32(output, section);
        long sharedEnd = dexEnd + u32(output, section + 4);
        check(sharedEnd <= output.length);
        long dex = section + 12;

        for (long i = 0; i < count; i++) {
            dex += 4;
            check(dex + 136 <= dexEnd && magic(output, dex, "cdex001\0"));
            long size = u32(output, dex + 32);
            long classCount = u32(output, dex + 96);
            long classOffset = u32(output, dex + 100);
            long data = dex + u32(output, dex + 108);
            long dataEnd = data + u32(output, dex + 104);
            check(
                    size >= 136
                            && dex + size <= dexEnd
                            && classOffset + classCount * 32 <= size
                            && data == dexEnd
                            && dataEnd <= sharedEnd);

            for (long c = 0; c < classCount; c++) {
                long classData = u32(output, dex + classOffset + c * 32 + 24);

                if (classData == 0) {
                    continue;
                }

                Leb leb = new Leb(output, data, dataEnd, data + classData);
                long fields = leb.read() + leb.read();
                long methods = leb.read() + leb.read();
                check(fields + methods <= dataEnd - leb.cursor);

                for (long member = 0; member < fields + methods; member++) {
                    leb.read();
                    long at = leb.cursor;
                    long encoded = leb.read();
                    boolean method = member >= fields;
                    long visibility = encoded & 7;
                    long decoded = encoded;

                    if (visibility != 0 && (visibility & (visibility - 1)) != 0) {
                        decoded ^= 7;
                    }

                    decoded &= ~(method && (encoded & 0x100) != 0 ? 0x200L : 0x20L);

                    if (decoded != encoded) {
                        long value = decoded;

                        for (long offset = at; offset < leb.cursor; offset++) {
                            output[(int) offset] =
                                    (byte) ((value & 127) | (offset + 1 < leb.cursor ? 128 : 0));
                            value >>>= 7;
                        }

                        check(value == 0);
                    }

                    if (method) {
                        leb.read();
                    }
                }
            }

            dex = (dex + size + 3) & ~3L;
        }

        check(dex == dexEnd);

        return output;
    }
}
