package com.voyemu.prepare;

import java.nio.file.Path;

/**
 * Passion starts every platform stream at its minimum and each volume group at zero; later volume
 * controls stay stock. Only the profile identity differs from the Free A11 7.3.1 recipe, so the
 * rewrite itself delegates to the same instruction sequence.
 */
final class PassionStartupVolumeGuard {
    static final String NAME = "passion-api30-startup-volume", DEX = "classes.dex";
    static final String PIN = "c6992cdc2a3e23d5aeb6ab77e7a7f6889cfcf5c262c6ed86825e6fcbf601681d";

    private PassionStartupVolumeGuard() {}

    static void need(boolean accepted) {
        Failure.require(
                accepted,
                "PASSION_STARTUP_VOLUME_CONTRACT",
                "Exact Passion cold-start volume initialization required",
                3);
    }

    static void validate(String profile, int api, Path original, byte[] dex) throws Exception {
        need(api == 30 && dex != null && PIN.equals(FilesSupport.hash(original)));
        StartupVolumeGuard.rewriter().validate(PassionServiceForegroundGuard.classes(dex));
    }

    static void validateOriginalChain(String profile, int api, Path original, byte[] dex)
            throws Exception {
        need(api == 30 && PIN.equals(FilesSupport.hash(original)) && dex != null);
        validate(
                profile,
                api,
                original,
                PassionDisplayPowerGuard.apply(profile, api, original, dex));
    }

    // Retained original-input identity name used by the guard test harness; identical contract to
    // validate.
    static void validateOriginal(String profile, int api, Path original, byte[] dex)
            throws Exception {
        validate(profile, api, original, dex);
    }

    static byte[] apply(String profile, int api, Path original, byte[] predecessor)
            throws Exception {
        need(api == 30 && predecessor != null && PIN.equals(FilesSupport.hash(original)));

        return StartupVolumeGuard.rewriter()
                .applyBytes(PassionServiceForegroundGuard.classes(predecessor));
    }
}
