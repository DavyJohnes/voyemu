package com.voyemu.prepare;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.Map;

/** Negative guards run in both the build JVM and the resulting native executable. */
final class NativeCoreTest {
    interface Action {
        void run() throws Exception;
    }

    static void rejected(Action action, String code) throws Exception {
        try {
            action.run();
        } catch (IllegalArgumentException error) {
            if (error.getMessage().equals(code)) {
                return;
            }

            throw error;
        }

        throw new AssertionError("Expected " + code);
    }

    static void u32(byte[] bytes, int at, int value) {
        for (int i = 0; i < 4; i++) {
            bytes[at + i] = (byte) (value >>> (i * 8));
        }
    }

    static byte[] fixture() {
        byte[] bytes = new byte[256];

        System.arraycopy(
                ("vdex019\0" + "002\0").getBytes(StandardCharsets.ISO_8859_1), 0, bytes, 0, 12);
        u32(bytes, 12, 1);
        u32(bytes, 24, 176);
        u32(bytes, 28, 44);
        System.arraycopy("cdex001\0".getBytes(StandardCharsets.ISO_8859_1), 0, bytes, 40, 8);
        u32(bytes, 72, 172);
        u32(bytes, 136, 1);
        u32(bytes, 140, 136);
        u32(bytes, 144, 44);
        u32(bytes, 148, 172);
        u32(bytes, 200, 1);
        bytes[213] = 1;
        bytes[214] = 0;
        bytes[215] = 1;
        bytes[216] = 0;
        bytes[217] = 0;
        bytes[218] = 0x26;
        bytes[219] = 0;
        bytes[220] = (byte) 0x86;
        bytes[221] = 0x06;
        bytes[222] = 0;

        return bytes;
    }

    static void run() throws Exception {
        NativeCore.check(
                org.jf.dexlib2.AccessFlags.getAccessFlag("public")
                                == org.jf.dexlib2.AccessFlags.PUBLIC
                        && org.jf.dexlib2.AccessFlags.getAccessFlagsForClass(1).length == 1,
                "TEST_FULL_DEXLIB_API");
        byte[] original = fixture();
        byte[] before = original.clone();
        byte[] decoded = BootVdex.decode(original);
        NativeCore.check(Arrays.equals(original, before), "TEST_INPUT_MUTATED");
        NativeCore.check(
                decoded[218] == 1 && decoded[220] == (byte) 0x81 && decoded[221] == 2,
                "TEST_HIDDEN_API_FLAGS");
        rejected(() -> BootVdex.decode(Arrays.copyOf(original, 30)), "FRAMEWORK_VDEX_INVALID");
        byte[] extent = original.clone();
        u32(extent, 28, Integer.MAX_VALUE);
        rejected(() -> BootVdex.decode(extent), "FRAMEWORK_VDEX_INVALID");
        byte[] leb = original.clone();
        Arrays.fill(leb, 220, 225, (byte) 255);
        rejected(() -> BootVdex.decode(leb), "FRAMEWORK_VDEX_INVALID");

        for (String path :
                new String[] {"../escape", "/absolute", "C:\\escape", "safe/../escape", "a\0b"}) {
            rejected(() -> NativeCore.safeEntry(path), "ZIP_ENTRY_INVALID");
        }

        Path directory = Files.createTempDirectory("voyemu-native-guard-");

        try {
            NativeCore.check(
                    Files.size(NativeCore.fixtureDex(directory)) > 112, "TEST_OWN_FIXTURE_DEX");
            Path input = directory.resolve("input");
            Files.writeString(input, "guard");
            rejected(() -> NativeCore.pinned(input, "0".repeat(64)), "INPUT_SHA_MISMATCH");
            Path zip = directory.resolve("aligned.zip");
            NativeCore.zip(
                    zip,
                    Map.of(
                            "resources.arsc",
                            new byte[] {1, 2, 3},
                            "lib/arm64-v8a/probe.so",
                            new byte[] {4, 5}),
                    true);
            NativeCore.check(NativeCore.entries(zip).size() == 2, "TEST_ZIP_ROUNDTRIP");
            byte[] bytes = Files.readAllBytes(zip);

            for (int at = 0; at + 30 < bytes.length; at++) {
                if (bytes[at] != 0x50
                        || bytes[at + 1] != 0x4b
                        || bytes[at + 2] != 3
                        || bytes[at + 3] != 4) {
                    continue;
                }

                int name = (bytes[at + 26] & 255) | ((bytes[at + 27] & 255) << 8);
                int extra = (bytes[at + 28] & 255) | ((bytes[at + 29] & 255) << 8);
                String entry = new String(bytes, at + 30, name, StandardCharsets.UTF_8);

                if (extra > 0) {
                    int field = at + 30 + name;
                    int tag = (bytes[field] & 255) | ((bytes[field + 1] & 255) << 8);
                    int size = (bytes[field + 2] & 255) | ((bytes[field + 3] & 255) << 8);
                    int alignment = (bytes[field + 4] & 255) | ((bytes[field + 5] & 255) << 8);
                    NativeCore.check(
                            tag == 0xd935
                                    && size == extra - 4
                                    && size >= 2
                                    && alignment == (entry.endsWith(".so") ? 4096 : 4),
                            "TEST_ZIP_ALIGNMENT_METADATA:" + entry);
                }

                NativeCore.check(
                        (at + 30 + name + extra) % (entry.endsWith(".so") ? 4096 : 4) == 0,
                        "TEST_ZIP_ALIGNMENT:"
                                + entry
                                + ":offset="
                                + (at + 30 + name + extra)
                                + ":extra="
                                + extra);
            }
        } finally {
            try (var files = Files.walk(directory)) {
                for (Path path : files.sorted(java.util.Comparator.reverseOrder()).toList()) {
                    Files.delete(path);
                }
            }
        }

        System.out.println(
                "NativeCore guards passed: hidden API, bounds, malformed ULEB, path traversal, SHA,"
                        + " ZIP alignment");
    }
}
