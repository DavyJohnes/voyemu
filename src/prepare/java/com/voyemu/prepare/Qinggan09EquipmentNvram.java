package com.voyemu.prepare;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/** Explicit simulation, not a physical equipment capture. Both readers use one item51 seed. */
final class Qinggan09EquipmentNvram {
    static final int SIZE = 4 * 1024 * 1024;
    static final int PRIMARY = 0x860, MIRROR = 0x20860;

    static byte[] create(String seed, String powertrain) {
        byte[] record = equipment(seed, powertrain);
        byte[] image = new byte[SIZE];

        image[0] = 1;
        System.arraycopy(record, 0, image, PRIMARY, 32);
        System.arraycopy(record, 0, image, MIRROR, 32);
        validate(image, seed, powertrain);

        return image;
    }

    static void validate(byte[] image, String seed, String powertrain) {
        require(image.length == SIZE, "Four MiB equipment image required");
        require(image[0] == 1 || image[0] == 2, "Equipment header must be 1 or 2");
        require(
                Byte.toUnsignedInt(image[0x11e0]) <= 3 && Byte.toUnsignedInt(image[0x211e0]) <= 3,
                "Equipment bank markers must be in 0..3");
        byte[] expected = equipment(seed, powertrain);
        require(
                Arrays.equals(expected, Arrays.copyOfRange(image, PRIMARY, PRIMARY + 32))
                        && Arrays.equals(expected, Arrays.copyOfRange(image, MIRROR, MIRROR + 32)),
                "Both equipment banks must equal declared simulation seed");
    }

    private static byte[] equipment(String seed, String powertrain) {
        require(
                "hybrid".equals(powertrain) || "ev".equals(powertrain),
                "Explicit hybrid or ev powertrain required");
        String value = null;

        for (String line : seed.split("\\n", -1)) {
            if (!line.startsWith("51=")) {
                continue;
            }

            require(value == null, "Duplicate equipment item51");
            value = line.substring(3);
        }

        require(
                value != null && value.matches("[0-9a-fA-F]{26,30}") && value.length() % 2 == 0,
                "Item51 requires 26..30 even ASCII hex characters and NUL padding");
        int transmission = Integer.parseInt(value.substring(0, 2), 16) & 3;
        require(
                transmission == ("hybrid".equals(powertrain) ? 1 : 0),
                "Item51 contradicts declared powertrain");
        byte[] record = new byte[32];
        byte[] text = value.getBytes(StandardCharsets.US_ASCII);
        System.arraycopy(text, 0, record, 0, text.length);

        return record;
    }

    private static void require(boolean condition, String message) {
        Failure.require(condition, "EQUIPMENT_SIMULATION_INVALID", message, 2);
    }
}
