package com.voyemu.prepare;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.List;

public final class ManifestLibrariesTest {
    static ByteBuffer b(byte[] bytes) {
        return ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
    }

    static byte[] fixture() throws Exception {
        List<String> values =
                List.of(
                        "android",
                        "http://schemas.android.com/apk/res/android",
                        "name",
                        "manifest",
                        "application",
                        "service",
                        "own.Service",
                        "Русский текст",
                        "exported");
        int start = 28 + values.size() * 4, size = start;

        for (String value : values) {
            size += 4 + value.length() * 2;
        }

        byte[] pool = new byte[(size + 3) & ~3];
        ByteBuffer data = b(pool);
        data.putShort(0, (short) 1);
        data.putShort(2, (short) 28);
        data.putInt(4, pool.length);
        data.putInt(8, values.size());
        data.putInt(20, start);
        int cursor = start;

        for (int i = 0; i < values.size(); i++) {
            String value = values.get(i);
            byte[] encoded = value.getBytes(StandardCharsets.UTF_16LE);
            data.putInt(28 + i * 4, cursor - start);
            data.putShort(cursor, (short) value.length());
            System.arraycopy(encoded, 0, pool, cursor + 2, encoded.length);
            cursor += encoded.length + 4;
        }

        byte[] resource = new byte[44];
        b(resource)
                .putShort(0, (short) 0x180)
                .putShort(2, (short) 8)
                .putInt(4, 44)
                .putInt(16, 0x01010003)
                .putInt(40, 0x01010010);
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        out.write(new byte[] {3, 0, 8, 0, 0, 0, 0, 0});
        out.write(pool);
        out.write(resource);

        for (int key : new int[] {3, 4, 5, -5, -4, -3}) {
            boolean end = key < 0;
            byte[] element = new byte[end ? 24 : 36];
            ByteBuffer info = b(element);
            info.putShort(0, (short) (end ? 0x103 : 0x102));
            info.putShort(2, (short) 16);
            info.putInt(4, element.length);
            info.putInt(8, 10);
            info.putInt(12, -1);
            info.putInt(16, -1);
            info.putInt(20, Math.abs(key));

            if (!end) {
                info.putShort(24, (short) 20);
                info.putShort(26, (short) 20);
            }

            out.write(element);
        }

        byte[] bytes = out.toByteArray();
        b(bytes).putInt(4, bytes.length);

        return bytes;
    }

    public static void main(String[] args) throws Exception {
        byte[] input = fixture(), snapshot = input.clone();
        byte[] output = ManifestLibraries.add(input, List.of("QGAPI", "QGMOD", "QGVehicle"));

        StorageJsonTest.check(
                Arrays.equals(input, snapshot)
                        && b(output).getInt(4) == output.length
                        && output.length > input.length);

        try {
            ManifestLibraries.add(output, List.of("QGAPI"));
            throw new AssertionError();
        } catch (Failure expected) {
            if (!expected.code.equals("PATCH_MANIFEST_INVALID")) {
                throw expected;
            }
        }

        for (byte[] bad : List.of(new byte[0], Arrays.copyOf(input, input.length - 1))) {
            try {
                ManifestLibraries.add(bad, List.of("QGAPI"));
                throw new AssertionError();
            } catch (Failure expected) {
                if (!expected.code.equals("PATCH_MANIFEST_INVALID")) {
                    throw expected;
                }
            }
        }

        byte[] services = ManifestLibraries.addServices(input, List.of("own.NewBrowser"));
        StorageJsonTest.check(Arrays.equals(input, snapshot) && services.length > input.length);
        int exportedDeclarations = 0;

        for (int offset = 8; offset < services.length; offset += b(services).getInt(offset + 4)) {
            if (Short.toUnsignedInt(b(services).getShort(offset)) != 0x102) {
                continue;
            }

            int attributes = Short.toUnsignedInt(b(services).getShort(offset + 28));

            for (int i = 0; i < attributes; i++) {
                int at = offset + 36 + i * 20;

                if (b(services).getInt(at + 4) == 8) {
                    StorageJsonTest.check(
                            services[at + 15] == 0x12 && b(services).getInt(at + 16) == -1);
                    exportedDeclarations++;
                }
            }
        }

        StorageJsonTest.check(exportedDeclarations == 1);

        try {
            ManifestLibraries.addServices(services, List.of("own.NewBrowser"));
            throw new AssertionError();
        } catch (Failure expected) {
            if (!expected.code.equals("PATCH_MANIFEST_INVALID")) {
                throw expected;
            }
        }

        System.out.println("Binary XML append, original preservation and duplicate guards passed");
    }
}
