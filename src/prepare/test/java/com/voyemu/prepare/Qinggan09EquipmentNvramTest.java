package com.voyemu.prepare;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;

public final class Qinggan09EquipmentNvramTest {
    private static int checks;

    public static void main(String[] args) throws Exception {
        checkSimulationImage();
        checkProfileSeeds(args);
        System.out.println(
                "A9 equipment simulation PASS: "
                        + checks
                        + " checks; live Enforcing root-read gate NOT RUN");
    }

    private static void checkSimulationImage() throws Exception {
        String hybrid = "51=1911F08F808000300000000001\n";
        byte[] image = Qinggan09EquipmentNvram.create(hybrid, "hybrid");

        check(
                image.length == 4194304 && image[0] == 1,
                "Physical partition extent and valid header");
        check(
                java.util.HexFormat.of()
                        .formatHex(java.security.MessageDigest.getInstance("SHA-256").digest(image))
                        .equals("8faa88acffee103b6d6a9ebb47c59e6fcc2eca9469084abf4e76154364bec079"),
                "Cross-language canonical simulation bytes");
        byte[] record = Arrays.copyOfRange(image, 0x860, 0x880);
        check(
                new String(record, 0, 26, StandardCharsets.US_ASCII)
                        .equals("1911F08F808000300000000001"),
                "All declared option bits preserved");
        check(
                Arrays.equals(record, Arrays.copyOfRange(image, 0x20860, 0x20880))
                        && record[26] == 0
                        && record[31] == 0,
                "Both banks and NUL tail");

        for (byte header : new byte[] {1, 2}) {
            for (byte primary = 0; primary <= 3; primary++) {
                for (byte mirror = 0; mirror <= 3; mirror++) {
                    image[0] = header;
                    image[0x11e0] = primary;
                    image[0x211e0] = mirror;
                    Qinggan09EquipmentNvram.validate(image, hybrid, "hybrid");
                    checks++;
                }
            }
        }

        image = Qinggan09EquipmentNvram.create(hybrid, "hybrid");
        byte[] valid = image;

        for (byte header : new byte[] {0, 3, -1}) {
            reject(() -> {
                byte[] b = valid.clone();
                b[0] = header;
                Qinggan09EquipmentNvram.validate(b, hybrid, "hybrid");
            });
        }

        for (int offset : new int[] {0x11e0, 0x211e0}) {
            for (byte marker : new byte[] {4, -1}) {
                reject(() -> {
                    byte[] b = valid.clone();
                    b[offset] = marker;
                    Qinggan09EquipmentNvram.validate(b, hybrid, "hybrid");
                });
            }
        }

        for (int offset : new int[] {0x860, 0x20860, 0x860 + 26, 0x20860 + 31}) {
            reject(() -> {
                byte[] b = valid.clone();
                b[offset] ^= 1;
                Qinggan09EquipmentNvram.validate(b, hybrid, "hybrid");
            });
        }

        reject(() ->
                Qinggan09EquipmentNvram.validate(
                        Arrays.copyOf(valid, valid.length - 1), hybrid, "hybrid"));
        reject(() -> Qinggan09EquipmentNvram.create(hybrid, "ev"));

        for (String kind : new String[] {"", "unknown", "Hybrid"}) {
            reject(() -> Qinggan09EquipmentNvram.create(hybrid, kind));
        }

        for (String value :
                new String[] {
                    "",
                    "0",
                    "01",
                    "0100000000000000000000000",
                    "010000000000000000000000000",
                    "01000000000000000000000000000000",
                    "G1000000000000000000000000",
                    "01000000000000000000000000\0",
                    "02000000000000000000000000",
                    "03000000000000000000000000"
                }) {
            reject(() -> Qinggan09EquipmentNvram.create("51=" + value + "\n", "hybrid"));
        }

        reject(() -> Qinggan09EquipmentNvram.create(hybrid + hybrid, "hybrid"));
        reject(() -> Qinggan09EquipmentNvram.create("1=not-equipment\n", "hybrid"));
        Qinggan09EquipmentNvram.create("51=18000000000000000000000000\n", "ev");
        checks++;
    }

    private static void checkProfileSeeds(String[] args) throws Exception {
        String seed = null;
        Path root = Path.of(args[0]);
        var profiles = new java.util.ArrayList<Path>();

        profiles.add(root.resolve("profiles/free-09-chinese-20260106-6.0.3"));

        for (int i = 1; i < args.length; i++) {
            profiles.add(Path.of(args[i]));
        }

        for (Path profile : profiles) {
            String text = Files.readString(profile.resolve("profile.json"));
            var data = Json.object(Json.parse(text));
            var definitions = Json.obj(data, "files");
            var raw = Json.obj(definitions, "hybrid-nvram");
            check(
                    raw.get("file").equals(Json.obj(definitions, "nvram").get("file")),
                    "JNI and raw share seed");
            var simulation = Json.obj(raw, "simulation");
            check(
                    "qinggan09-equipment-item51-v1".equals(simulation.get("kind"))
                            && "hybrid".equals(simulation.get("powertrain")),
                    "Explicit simulation and powertrain");
            String current =
                    Files.readString(profile.resolve("files").resolve(Json.str(raw, "file")));

            if (seed == null) {
                seed = current;
            }

            check(seed.equals(current), "Functional clones share equipment options");
            Qinggan09EquipmentNvram.validate(
                    Qinggan09EquipmentNvram.create(current, "hybrid"), current, "hybrid");
            check(!text.contains("persist.sys.qg.powertrain"), "No property fallback");
        }
    }

    private static void check(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }

        checks++;
    }

    private static void reject(Runnable action) {
        try {
            action.run();
        } catch (Failure expected) {
            check(expected.code.equals("EQUIPMENT_SIMULATION_INVALID"), "Typed simulation failure");

            return;
        }

        throw new AssertionError("Invalid equipment accepted");
    }
}
