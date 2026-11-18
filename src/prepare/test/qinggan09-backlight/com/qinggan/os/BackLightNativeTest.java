package com.qinggan.os;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

/**
 * Exercises the real JNI exports, their shared file store seeded on first use and the existing
 * numeric NVRAM API.
 */
public final class BackLightNativeTest {
    private static void need(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static void verifyBounds(Path nvram) throws Exception {
        byte[] before = Files.readAllBytes(nvram);

        need(BackLightNative.native_set_mode(2, 1) < 0, "invalid mode accepted");
        need(
                BackLightNative.native_set_mode(1, -1) < 0
                        && BackLightNative.native_get_mode(5) < 0,
                "invalid screen accepted");
        need(
                BackLightNative.native_set_backlight_for_screen(256, 0) < 0
                        && BackLightNative.native_set_backlight(-1) < 0,
                "invalid brightness accepted");
        need(
                BackLightNative.native_set_key_state(2) < 0
                        && BackLightNative.native_set_stm8_state(-1) < 0
                        && BackLightNative.native_slidebar_set_state(2) < 0,
                "invalid state accepted");
        need(
                java.util.Arrays.equals(before, Files.readAllBytes(nvram)),
                "rejected writes changed NVRAM");
    }

    public static void main(String[] args) throws Exception {
        BackLightNative.load(args[0]);
        Path nvram = Path.of(System.getenv("VOYEMU_TEST_NVRAM"));
        Path seed = Path.of(System.getenv("VOYEMU_TEST_NVRAM_SEED"));

        if (args.length == 2 && args[1].equals("first")) {
            need(NvramNative.native_nvramread(43).equals("equipment"), "first use lost the seed");

            return;
        }

        if (args.length == 2 && args[1].equals("child")) {
            need(BackLightNative.native_get_mode(1) == 0, "mode not shared with child JVM");
            need(
                    BackLightNative.native_set_backlight_for_screen(73, 2) == 0,
                    "child brightness write");
            need(NvramNative.native_nvramwrite("child", 43) == 0, "child numeric NVRAM write");

            return;
        }

        verifySeededStore(args[0], nvram, seed);
        verifyBacklightExports();
        verifyBounds(nvram);
        verifySharedNvram(args[0], nvram);
        verifyMissingSeed(nvram, seed);
        System.out.println(
                "BackLight JNI PASS: all 24 exports, one seeded store for concurrent first callers,"
                        + " shared cross-JVM state, numeric/unknown NVRAM preservation, bounds and"
                        + " missing-seed failures");
    }

    private static void verifySeededStore(String library, Path nvram, Path seed) throws Exception {
        // Concurrent first callers publish one complete copy of the seed and leave it unchanged.
        need(!Files.exists(nvram), "store must start absent");
        List<Process> firsts = new java.util.ArrayList<>();

        for (int index = 0; index < 6; index++) {
            firsts.add(start(library, "first"));
        }

        for (Process first : firsts) {
            need(
                    first.waitFor(15, java.util.concurrent.TimeUnit.SECONDS)
                            && first.exitValue() == 0,
                    "concurrent first caller failed");
        }

        need(
                java.util.Arrays.equals(Files.readAllBytes(seed), Files.readAllBytes(nvram))
                        && !Files.exists(nvram.resolveSibling(nvram.getFileName() + ".seed")),
                "store differs from its seed or kept a partial copy");
        need(
                Files.getPosixFilePermissions(nvram)
                        .equals(
                                java.nio.file.attribute.PosixFilePermissions.fromString(
                                        "rw-rw----")),
                "store mode must be 0660");
    }

    private static void verifyBacklightExports() throws Exception {
        // This first call reproduces the actual SystemPolicy failure on the old JNI library.
        need(BackLightNative.native_set_mode(0, 1) == 0, "mode write");
        need(BackLightNative.native_get_mode(1) == 0, "mode readback");
        need(BackLightNative.native_get_mode(0) == 1, "independent default primary mode");

        for (int screen = 0; screen < 5; screen++) {
            int brightness = 37 + screen;
            need(
                    BackLightNative.native_set_backlight_for_screen(brightness, screen) == 0,
                    "screen brightness write");
            need(
                    BackLightNative.native_get_backlight_for_screen(screen) == brightness,
                    "screen brightness readback");
            need(
                    BackLightNative.native_close_backlight_for_screen(screen) == 0,
                    "screen gate close");
            need(
                    BackLightNative.native_get_screen_state_for_screen(screen) == 1,
                    "screen gate closed");
            need(BackLightNative.native_open_backlight_for_screen(screen) == 0, "screen gate open");
            need(
                    BackLightNative.native_get_screen_state_for_screen(screen) == 0,
                    "screen gate opened");
        }

        need(
                BackLightNative.native_set_backlight(123) == 0
                        && BackLightNative.native_get_backlight() == 123,
                "primary brightness");
        need(
                BackLightNative.native_close_backlight() == 0
                        && BackLightNative.native_get_screen_state() == 1,
                "primary gate close");
        need(
                BackLightNative.native_open_backlight() == 0
                        && BackLightNative.native_get_screen_state() == 0,
                "primary gate open");
        need(
                BackLightNative.native_close_brightness() == 0
                        && BackLightNative.native_get_screen_state() == 1,
                "brightness gate close");
        need(
                BackLightNative.native_open_brightness() == 0
                        && BackLightNative.native_get_screen_state() == 0,
                "brightness gate open");
        need(
                BackLightNative.native_set_keypad_backlight(81) == 0
                        && BackLightNative.native_get_key_backlight() == 81,
                "key brightness");
        need(
                BackLightNative.native_set_key_state(0) == 0
                        && BackLightNative.native_get_key_state() == 0,
                "key state");
        need(
                BackLightNative.native_backlight_set_stm8(93) == 0
                        && BackLightNative.native_backlight_get_stm8() == 93,
                "STM8 brightness");
        need(
                BackLightNative.native_set_stm8_state(1) == 0
                        && BackLightNative.native_get_stm8_state() == 1,
                "STM8 state");
        need(BackLightNative.native_slidebar_set_state(0) == 0, "slidebar state");
        need(
                BackLightNative.native_get_backlight_ver().equals("voyemu-backlight-v1"),
                "explicit virtual driver version");
    }

    private static void verifySharedNvram(String library, Path nvram) throws Exception {
        need(
                NvramNative.native_nvramwrite("parent", 51) == 0,
                "numeric write after namespaced writes");
        need(BackLightNative.native_get_mode(1) == 0, "numeric write dropped backlight record");
        Process child = start(library, "child");
        need(
                child.waitFor(15, java.util.concurrent.TimeUnit.SECONDS) && child.exitValue() == 0,
                "child JVM failed");
        need(
                BackLightNative.native_get_backlight_for_screen(2) == 73,
                "cross-process brightness lost");
        need(
                NvramNative.native_nvramread(43).equals("child")
                        && NvramNative.native_nvramread(51).equals("parent"),
                "numeric values lost");
        need(Files.readString(nvram).contains("foreign.record=keep\n"), "unknown record lost");
        need(NvramNative.native_nvramwrite("kept", 43) == 0, "write before reseed check");
        need(NvramNative.native_nvramread(43).equals("kept"), "existing store replaced by seed");
    }

    private static void verifyMissingSeed(Path nvram, Path seed) throws Exception {
        Path saved = nvram.resolveSibling("saved-nvram"),
                savedSeed = seed.resolveSibling("saved-seed");

        Files.move(nvram, saved);
        Files.move(seed, savedSeed);
        need(
                BackLightNative.native_set_mode(1, 0) < 0
                        && BackLightNative.native_get_backlight() < 0
                        && !Files.exists(nvram),
                "store without seed must fail, never create");
        Files.move(savedSeed, seed);
        Files.move(saved, nvram);
    }

    private static Process start(String library, String mode) throws Exception {
        return new ProcessBuilder(
                        List.of(
                                Path.of(System.getProperty("java.home"), "bin", "java").toString(),
                                "-cp",
                                System.getProperty("java.class.path"),
                                BackLightNativeTest.class.getName(),
                                library,
                                mode))
                .inheritIO()
                .start();
    }
}
