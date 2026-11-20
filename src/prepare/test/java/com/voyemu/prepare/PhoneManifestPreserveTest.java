package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipFile;

/** Normal-source test; arguments are genuine executable APK and resource-only theme APK. */
public final class PhoneManifestPreserveTest {
    private static final String ORIGINAL =
            "c8a33e377a6a708ef6c3cca7e8cc3f938349c04dd494dac32bcf823a3096efa1";
    private static final Map<String, Object> CONSTANTS =
            Map.of(StepConstants.PINS, List.of(ORIGINAL));

    private static byte[] manifest(Path apk) throws Exception {
        try (ZipFile zip = new ZipFile(apk.toFile())) {
            return zip.getInputStream(zip.getEntry("AndroidManifest.xml")).readAllBytes();
        }
    }

    private static void reject(
            Map<String, Object> constants, int api, Path apk, byte[] input, String code)
            throws Exception {
        try {
            PhoneProviderGuard.manifest(constants, api, "arm64-v8a", apk, input);
            throw new AssertionError("Accepted unsupported manifest contract");
        } catch (Failure expected) {
            if (!code.equals(expected.code)) {
                throw new AssertionError(expected.code);
            }
        }
    }

    static void preserve(byte[] original, byte[] output) {
        PhoneManifestProvidersTest.need(
                PhoneManifestProvidersTest.size(output, 0) == output.length);
        int cursor = 8, oldCount = 0, newCount = 0;

        for (int at = 8; at < original.length; ) {
            int size = PhoneManifestProvidersTest.size(original, at),
                    type = PhoneManifestProvidersTest.type(original, at);
            PhoneManifestProvidersTest.need(size >= 8 && size <= original.length - at);

            if (type != 1 && type != 0x180) {
                byte[] chunk = Arrays.copyOfRange(original, at, at + size);
                boolean found = false;

                while (cursor < output.length) {
                    int nextSize = PhoneManifestProvidersTest.size(output, cursor);
                    PhoneManifestProvidersTest.need(
                            nextSize >= 8 && nextSize <= output.length - cursor);
                    int next = cursor + nextSize;

                    if (Arrays.equals(chunk, Arrays.copyOfRange(output, cursor, next))) {
                        found = true;
                        cursor = next;
                        break;
                    }

                    cursor = next;
                }

                PhoneManifestProvidersTest.need(found);
                oldCount++;
            }

            at += size;
        }

        for (int at = 8; at < output.length; ) {
            int size = PhoneManifestProvidersTest.size(output, at),
                    type = PhoneManifestProvidersTest.type(output, at);
            PhoneManifestProvidersTest.need(size >= 8 && size <= output.length - at);

            if (type != 1 && type != 0x180) {
                newCount++;
            }

            at += size;
        }

        PhoneManifestProvidersTest.need(newCount == oldCount + 4);
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 2) {
            throw new AssertionError("genuine executable APK and theme APK required");
        }

        Path apk = Path.of(args[0]);

        if (!ORIGINAL.equals(FilesSupport.hash(apk))) {
            throw new AssertionError("Exact genuine executable required");
        }

        byte[] original = manifest(apk), saved = original.clone();
        byte[] output = PhoneProviderGuard.manifest(CONSTANTS, 30, "arm64-v8a", apk, original);

        if (!Arrays.equals(saved, original) || !Arrays.equals(output, PhoneManifest.add(saved))) {
            throw new AssertionError("Shared manifest engine or original changed");
        }

        PhoneManifestProvidersTest.providerDeclarations(output);
        preserve(original, output);
        reject(CONSTANTS, 28, apk, original, "PHONE_PROVIDER_CONTRACT");
        reject(Map.of(StepConstants.PINS, List.of()), 30, apk, original, "PHONE_PROVIDER_CONTRACT");
        reject(CONSTANTS, 30, apk, manifest(Path.of(args[1])), "PATCH_MANIFEST_INVALID");
        reject(CONSTANTS, 30, apk, output, "PATCH_MANIFEST_INVALID");
        reject(
                CONSTANTS,
                30,
                apk,
                Arrays.copyOf(original, original.length - 1),
                "PATCH_MANIFEST_INVALID");
        System.out.println(
                "PASS exact genuine input, shared append behavior, permissions,"
                        + " pin/API/theme/duplicate/truncation negatives");
    }
}
