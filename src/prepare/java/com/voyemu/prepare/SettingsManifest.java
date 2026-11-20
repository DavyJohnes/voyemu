package com.voyemu.prepare;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Path;
import java.util.Map;

/** Exact typed versionCode1 -> 2 so SYSTEM supersedes the previously preserved DATA1 APK. */
final class SettingsManifest {
    static void validate(Map<String, Object> constants, int api, Path original, byte[] manifest)
            throws Exception {
        Failure.require(
                api == 30
                        && StepConstants.admits(constants, FilesSupport.hash(original))
                        && manifest != null
                        && manifest.length == 17408
                        && manifest[9095] == 0x10
                        && ByteBuffer.wrap(manifest).order(ByteOrder.LITTLE_ENDIAN).getInt(9096)
                                == 1,
                "SETTINGS_MANIFEST",
                "Exact mapped Free A11 7.3.1 Setting versionCode1 manifest required",
                3);
    }

    static void validateOriginal(
            Map<String, Object> constants, int api, Path original, byte[] manifest)
            throws Exception {
        validate(
                constants,
                api,
                original,
                ManifestLibraries.add(manifest, java.util.List.of("QGAPI", "QGMOD", "QGVehicle")));
    }

    static byte[] apply(Map<String, Object> constants, int api, Path original, byte[] manifest)
            throws Exception {
        validate(constants, api, original, manifest);
        byte[] out = manifest.clone();
        ByteBuffer.wrap(out).order(ByteOrder.LITTLE_ENDIAN).putInt(9096, 2);

        return out;
    }
}
