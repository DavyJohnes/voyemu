package com.voyemu.prepare;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipFile;

public final class MediaCloudManifestGuardTest {
    static int negatives;

    interface Call {
        void run() throws Exception;
    }

    static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    static void reject(Call call) throws Exception {
        try {
            call.run();
            throw new AssertionError("accepted invalid input");
        } catch (Failure expected) {
            check(expected.code.equals("PATCH_MEDIA_CLOUD_MANIFEST_PIN"));
            negatives++;
        }
    }

    static ByteBuffer b(byte[] bytes) {
        return ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
    }

    public static void main(String[] args) throws Exception {
        Path original = Path.of(args[0]);
        Map<String, Object> constants =
                Map.of(StepConstants.PINS, List.of(FilesSupport.hash(original)));
        byte[] raw;

        try (ZipFile zip = new ZipFile(original.toFile())) {
            raw = zip.getInputStream(zip.getEntry("AndroidManifest.xml")).readAllBytes();
        }

        byte[] mapped = ManifestLibraries.add(raw, List.of("QGAPI", "QGMOD", "QGVehicle"));

        for (boolean rawInput : new boolean[] {true, false}) {
            byte[] input = rawInput ? raw : mapped;
            int removal = rawInput ? 14624 : 14688, enabled = removal + 1056;
            byte[] snapshot = input.clone();
            MediaCloudManifestGuard.validate(constants, 30, "arm64-v8a", original, input);
            byte[] output =
                    MediaCloudManifestGuard.apply(constants, 30, "arm64-v8a", original, input);
            Files.write(
                    Path.of(args[2], rawInput ? "raw-patched.bin" : "mapped-patched.bin"), output);
            check(Arrays.equals(snapshot, input));
            check(output.length == input.length - 100 && b(output).getInt(4) == output.length);
            check(
                    Short.toUnsignedInt(b(input).getShort(removal)) == 0x102
                            && b(input).getInt(removal + 4) == 76);
            check(
                    Short.toUnsignedInt(b(input).getShort(removal + 76)) == 0x103
                            && b(input).getInt(removal + 80) == 24);
            check(
                    input[enabled - 1] == 0x12
                            && b(input).getInt(enabled) == -1
                            && b(output).getInt(enabled - 100) == 0);
            // Exact inverse uses removed chunks from the OEM APK-derived input, not repository
            // literals.
            byte[] inverse = new byte[input.length];
            System.arraycopy(output, 0, inverse, 0, removal);
            System.arraycopy(input, removal, inverse, removal, 100);
            System.arraycopy(output, removal, inverse, removal + 100, output.length - removal);
            b(inverse).putInt(4, inverse.length).putInt(enabled, -1);
            check(
                    Arrays.equals(
                            input,
                            inverse)); // all providers, other metadata, permissions, services and
            // chunks preserved.
            reject(() ->
                    MediaCloudManifestGuard.apply(constants, 30, "arm64-v8a", original, output));

            for (int changed : new int[] {removal, enabled}) {
                byte[] bad = input.clone();
                bad[changed] ^= 1;
                reject(() ->
                        MediaCloudManifestGuard.apply(constants, 30, "arm64-v8a", original, bad));
            }
        }

        reject(() ->
                MediaCloudManifestGuard.apply(
                        Map.of(StepConstants.PINS, List.of("0".repeat(64))),
                        30,
                        "arm64-v8a",
                        original,
                        raw));
        reject(() -> MediaCloudManifestGuard.apply(constants, 28, "arm64-v8a", original, raw));
        reject(() -> MediaCloudManifestGuard.apply(constants, 30, "armeabi-v7a", original, raw));
        reject(() -> MediaCloudManifestGuard.apply(constants, 30, "arm64-v8a", original, null));
        reject(() ->
                MediaCloudManifestGuard.apply(
                        constants, 30, "arm64-v8a", original, Arrays.copyOf(raw, raw.length - 1)));
        reject(() ->
                MediaCloudManifestGuard.apply(constants, 30, "arm64-v8a", Path.of(args[1]), raw));
        System.out.println(
                "PASS raw and mapped exact inverses, immutable input, two-pass preflight/apply, "
                        + negatives
                        + " negative guards");
    }
}
