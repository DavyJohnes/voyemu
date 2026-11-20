package com.voyemu.prepare;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipFile;

public final class SettingsManifestTest {
    static void check(boolean b, String s) {
        if (!b) {
            throw new AssertionError(s);
        }
    }

    interface Action {
        void run() throws Exception;
    }

    static void reject(Action a) throws Exception {
        boolean denied = false;

        try {
            a.run();
        } catch (Failure e) {
            check(e.code.equals("SETTINGS_MANIFEST"), "exact failure");
            denied = true;
        }

        check(denied, "inverse accepted");
    }

    public static void main(String[] a) throws Exception {
        Path apk = Path.of(a[0]), job = Path.of(a[1]);
        Map<String, Object> constants = Map.of(StepConstants.PINS, List.of(FilesSupport.hash(apk)));
        byte[] raw;

        try (var z = new ZipFile(apk.toFile())) {
            raw = z.getInputStream(z.getEntry("AndroidManifest.xml")).readAllBytes();
        }

        byte[] mapped = ManifestLibraries.add(raw, List.of("QGAPI", "QGMOD", "QGVehicle")),
                out = SettingsManifest.apply(constants, 30, apk, mapped);
        check(
                ByteBuffer.wrap(mapped).order(ByteOrder.LITTLE_ENDIAN).getInt(9096) == 1
                        && ByteBuffer.wrap(out).order(ByteOrder.LITTLE_ENDIAN).getInt(9096) == 2,
                "monotonic 1->2");

        for (int i = 0; i < mapped.length; i++) {
            if (i < 9096 || i >= 9100) {
                check(mapped[i] == out[i], "foreign manifest byte " + i);
            }
        }

        check(
                mapped[9096] == 1
                        && out[9096] == 2
                        && Arrays.equals(
                                Arrays.copyOfRange(mapped, 9097, 9100),
                                Arrays.copyOfRange(out, 9097, 9100)),
                "only one typed data byte changed");
        reject(() -> SettingsManifest.apply(constants, 29, apk, mapped));
        reject(() -> SettingsManifest.apply(constants, 30, apk, raw));
        reject(() -> SettingsManifest.apply(constants, 30, apk, out));
        reject(() ->
                SettingsManifest.apply(
                        constants, 30, apk.resolveSibling("SystemService.apk"), mapped));
        String report =
                """
                mapped_v1_to_v2=PASS
                only_typed_version_data_byte_changed=1
                all_other_manifest_bytes_identical=true
                existing_QGAPI_QGMOD_QGVehicle_manifest_bytes_preserved=true
                inverse_guards=5
                """;
        Files.writeString(job.resolve("manifest-regression-receipt.txt"), report);
        System.out.print(report);
    }
}
