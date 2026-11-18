package com.voyemu.prepare;

import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.HexFormat;
import java.util.Map;

/** Exact ARM64 manifest availability patch of the profile; no XML reserialization. */
final class MediaCloudManifestGuard {
    private static final Map<String, String> VARIANTS =
            Map.of(
                    "free-11-chinese-20250710-6.11.1",
                    "c6f96ac40525aac9600731f7494cc3d23cb969c4a88ff2815526abe7da2e6d2e",
                    "free-11-chinese-20260731-7.3.1",
                    "533a0b0dc5ccbe91a42847fac20c5735bd8d78a28bbb980a6bf365dd5ed0fba0");

    /** Initializer offsets of the original manifest and of the one with added libraries. */
    private static final int[] OFFSETS = {14624, 14688};

    static void validate(String profile, int api, String abi, Path original, byte[] manifest)
            throws Exception {
        location(profile, api, abi, original, manifest);
    }

    static byte[] apply(String profile, int api, String abi, Path original, byte[] manifest)
            throws Exception {
        int offset = location(profile, api, abi, original, manifest);
        // Remove only the initializer's START_ELEMENT (76) and END_ELEMENT (24).
        byte[] output = new byte[manifest.length - 100];

        System.arraycopy(manifest, 0, output, 0, offset);
        System.arraycopy(manifest, offset + 100, output, offset, manifest.length - offset - 100);
        ByteBuffer buffer = ByteBuffer.wrap(output).order(ByteOrder.LITTLE_ENDIAN);
        buffer.putInt(4, output.length);
        // Only android:enabled typed boolean data of CloudListenBrowserService.
        buffer.putInt(offset + 1056 - 100, 0);

        return output;
    }

    private static int location(String profile, int api, String abi, Path original, byte[] manifest)
            throws Exception {
        require(VARIANTS.containsKey(profile) && api == 30 && "arm64-v8a".equals(abi), "scope");
        require(original != null && Files.isRegularFile(original), "original APK missing");
        MessageDigest digest = MessageDigest.getInstance("SHA-256");

        try (InputStream input = Files.newInputStream(original)) {
            byte[] block = new byte[65536];
            int count;

            while ((count = input.read(block)) != -1) {
                digest.update(block, 0, count);
            }
        }

        require(
                VARIANTS.get(profile).equals(HexFormat.of().formatHex(digest.digest())),
                "original APK SHA256");
        require(
                manifest != null
                        && manifest.length <= 20000
                        && manifest.length >= 8
                        && ByteBuffer.wrap(manifest).order(ByteOrder.LITTLE_ENDIAN).getInt(4)
                                == manifest.length,
                "manifest extent");
        int found = -1;

        for (int offset : OFFSETS) {
            if (initializer(manifest, offset)) {
                require(found < 0, "unique initializer");
                found = offset;
            }
        }

        require(found >= 0, "initializer element");

        return found;
    }

    /** START_ELEMENT (76) and END_ELEMENT (24) followed by the true android:enabled boolean. */
    private static boolean initializer(byte[] manifest, int offset) {
        if (offset + 1060 > manifest.length) {
            return false;
        }

        ByteBuffer buffer = ByteBuffer.wrap(manifest).order(ByteOrder.LITTLE_ENDIAN);

        return buffer.getShort(offset) == 0x0102
                && buffer.getShort(offset + 2) == 16
                && buffer.getInt(offset + 4) == 76
                && buffer.getShort(offset + 76) == 0x0103
                && buffer.getInt(offset + 80) == 24
                && manifest[offset + 1055] == 0x12
                && buffer.getInt(offset + 1056) == -1;
    }

    private static void require(boolean condition, String reason) {
        Failure.require(
                condition, "PATCH_MEDIA_CLOUD_MANIFEST_PIN", "Media manifest: " + reason, 5);
    }
}
