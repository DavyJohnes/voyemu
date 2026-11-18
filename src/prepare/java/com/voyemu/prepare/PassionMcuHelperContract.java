package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.Map;

/** Binds the admitted MCU helper ABI to its actual prepared APK output. */
final class PassionMcuHelperContract {
    static final String ARTIFACT = "patched:MonzaCarService";

    private PassionMcuHelperContract() {}

    static boolean requested(String profile, Map<String, Object> patches) {
        return PassionMcuPropertyGuard.PROFILE.equals(profile)
                && patches.containsKey("MonzaCarService")
                && Json.list(Json.obj(patches, "MonzaCarService").get("steps"))
                        .contains("passion-api30-mcu-property.json");
    }

    static Map<String, Object> receipt(Path archive) throws Exception {
        byte[] helper = NativeCore.entries(archive).get("classes18.dex");

        PassionMcuPropertyGuard.validateHelper(helper);

        return Map.of(
                "contract",
                "passion-mcu-property-helper-v1",
                "artifact",
                ARTIFACT,
                "archive_sha256",
                FilesSupport.hash(archive),
                "entry",
                "classes18.dex",
                "sha256",
                NativeCore.hash(helper),
                "bytes",
                (long) helper.length);
    }
}
