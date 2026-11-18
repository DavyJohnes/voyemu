package com.voyemu.prepare;

import java.nio.file.Path;

/**
 * Passion binding: retain stock navigation suppression and enable the canonical decorated secondary
 * status bar.
 */
final class PassionSecondaryNavigationGuard {
    static final String PROFILE = "passion-11-russian-20260403-2.1.4.80",
            NAME = "passion-api30-secondary-navigation";
    static final String PIN = Api30SecondaryNavigationPolicy.PIN;

    private static void profile(String profile) {
        Failure.require(
                PROFILE.equals(profile),
                "PASSION_SECONDARY_NAVIGATION_CONTRACT",
                "Exact Passion secondary navigation profile required",
                3);
    }

    static void validate(String profile, int api, Path original, byte[] dex) throws Exception {
        profile(profile);
        Api30SecondaryNavigationPolicy.validate(api, original, dex);
    }

    static byte[] apply(String profile, int api, Path original, byte[] dex) throws Exception {
        profile(profile);

        return Api30SecondaryNavigationPolicy.apply(api, original, dex, true);
    }
}
