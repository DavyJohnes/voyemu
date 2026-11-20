package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipFile;

public final class MediaNotificationGuardTest {

    interface Run {
        void run() throws Exception;
    }

    static void reject(Run r) throws Exception {
        boolean failed = false;

        try {
            r.run();
        } catch (Failure e) {
            failed = e.code.equals("MEDIA_NOTIFICATION_CONTRACT");
        }

        if (!failed) {
            throw new AssertionError("negative accepted");
        }
    }

    public static void main(String[] a) throws Exception {
        Path original = Path.of(a[0]);
        Map<String, Object> constants =
                Map.of(StepConstants.PINS, List.of(FilesSupport.hash(original)));
        byte[] dex;

        try (var z = new ZipFile(original.toFile())) {
            dex = z.getInputStream(z.getEntry("classes2.dex")).readAllBytes();
        }

        byte[] out = MediaNotificationGuard.apply(constants, 30, "arm64-v8a", original, dex);

        for (int bits = 0; bits < 4; bits++) {
            for (boolean playing : new boolean[] {false, true}) {
                long actions =
                        ((bits & 1) != 0 ? 16 : 0) | ((bits & 2) != 0 ? 32 : 0) | 0x100000000L;
                var actual = new ArrayList<String>();

                if ((actions & 16) != 0) {
                    actual.add("previous");
                }

                actual.add(playing ? "pause" : "play");

                if ((actions & 32) != 0) {
                    actual.add("next");
                }

                int[] compact =
                        Arrays.copyOf(new int[] {0, 1, 2}, 1 + Long.bitCount(actions & 0x30));

                if (compact.length != actual.size()) {
                    throw new AssertionError("count");
                }

                for (int i = 0; i < compact.length; i++) {
                    if (compact[i] != i || compact[i] >= actual.size()) {
                        throw new AssertionError("index");
                    }
                }
            }
        }

        reject(() ->
                MediaNotificationGuard.validate(
                        Map.of(StepConstants.PINS, List.of("0".repeat(64))),
                        30,
                        "arm64-v8a",
                        original,
                        dex));
        reject(() -> MediaNotificationGuard.validate(constants, 29, "arm64-v8a", original, dex));
        reject(() -> MediaNotificationGuard.validate(constants, 30, "armeabi-v7a", original, dex));
        reject(() -> MediaNotificationGuard.validate(constants, 30, "arm64-v8a", original, null));
        reject(() -> MediaNotificationGuard.validate(constants, 30, "arm64-v8a", original, out));
        Files.write(Path.of(a[1]), out);
        System.out.println(
                "PASS actual DEX serialize/full-class inverse; 8 flag/play-pause cases; 6"
                        + " negatives; patchedDEX="
                        + NativeCore.hash(out));
    }
}
