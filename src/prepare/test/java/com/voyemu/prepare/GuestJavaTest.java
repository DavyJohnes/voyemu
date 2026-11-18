package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.Map;
import java.util.TreeMap;

public final class GuestJavaTest {
    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static String code(Exception error) {
        return error instanceof Failure failure ? failure.code : error.toString();
    }

    public static void main(String[] args) throws Exception {
        Path platform = Path.of(args[0]), work = Path.of(args[1]);
        Path source = work.resolve("unit/src"), declarations = source.resolve("compile-only");

        Files.createDirectories(declarations);
        Files.writeString(
                declarations.resolve("Contract.java"),
                "package fixture; public class Contract { public int value() { return 3; } }");
        Files.writeString(
                source.resolve("Consumer.java"),
                "package fixture; public class Consumer { public int call(Contract c) { return"
                        + " c.value(); } }");
        GuestJava.Unit unit = new GuestJava.Unit(source, 30);
        String key = GuestJava.key(unit, platform);
        check(key.matches("[0-9a-f]{64}"), "the key is a SHA-256");
        check(
                !key.equals(GuestJava.key(new GuestJava.Unit(source, 28), platform)),
                "the minimum API is part of the key");
        byte[] compiled =
                Files.readAllBytes(
                        GuestJava.compile(unit, platform, work.resolve("compile"))
                                .resolve("classes.dex"));

        // The CI asset holds the DEX under the key of the unit inputs.
        Map<String, byte[]> entries = new TreeMap<>();
        entries.put(key + "/classes.dex", compiled);
        entries.put("index.json", "{}\n".getBytes(java.nio.charset.StandardCharsets.UTF_8));
        Path asset = work.resolve("android-java.zip");
        NativeCore.zip(asset, entries, false);
        GuestJava.use(asset);

        try {
            byte[] read =
                    Files.readAllBytes(
                            GuestJava.dex(unit, platform, work.resolve("asset"))
                                    .resolve("classes.dex"));
            check(Arrays.equals(read, compiled), "the asset DEX of the unit is used");

            // A changed source is a new unit that the asset does not hold.
            Files.writeString(
                    source.resolve("Consumer.java"),
                    "package fixture; public class Consumer { public int call(Contract c) {"
                            + " return c.value() + 1; } }");
            String missing = "";

            try {
                GuestJava.dex(unit, platform, work.resolve("changed"));
            } catch (Failure error) {
                missing = code(error);
            }

            check(missing.equals("ANDROID_JAVA_MISSING"), "a changed unit is missing: " + missing);
        } finally {
            GuestJava.use(null);
        }

        System.out.println("PASS GuestJavaTest");
    }
}
