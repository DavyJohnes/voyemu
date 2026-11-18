package com.voyemu.prepare;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Path;

/** Exact typed versionCode1 -> 2 so SYSTEM supersedes the previously preserved DATA1 APK. */
final class SettingsManifest {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1";

    static void validate(String profile, int api, Path original, byte[] manifest) throws Exception {
        Failure.require(
                PROFILE.equals(profile)
                        && api == 30
                        && SettingsVoiceCloneGuard.pins(PROFILE)
                                .contains(FilesSupport.hash(original))
                        && manifest != null
                        && manifest.length == 17408
                        && manifest[9095] == 0x10
                        && ByteBuffer.wrap(manifest).order(ByteOrder.LITTLE_ENDIAN).getInt(9096)
                                == 1,
                "SETTINGS_MANIFEST",
                "Exact mapped Free A11 7.3.1 Setting versionCode1 manifest required",
                3);
    }

    static void validateOriginal(String profile, int api, Path original, byte[] manifest)
            throws Exception {
        validate(
                profile,
                api,
                original,
                ManifestLibraries.add(manifest, java.util.List.of("QGAPI", "QGMOD", "QGVehicle")));
    }

    static byte[] apply(String profile, int api, Path original, byte[] manifest) throws Exception {
        validate(profile, api, original, manifest);
        byte[] out = manifest.clone();
        ByteBuffer.wrap(out).order(ByteOrder.LITTLE_ENDIAN).putInt(9096, 2);

        return out;
    }
}
