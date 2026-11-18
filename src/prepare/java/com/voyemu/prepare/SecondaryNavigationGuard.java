package com.voyemu.prepare;

import java.nio.file.Path;

/** Preserve the existing Free recipe and exact output while sharing the clean platform policy. */
final class SecondaryNavigationGuard {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1",
            NAME = "api30-secondary-navigation";
    static final String PIN = Api30SecondaryNavigationPolicy.PIN,
            TYPE = Api30SecondaryNavigationPolicy.TYPE;

    private static void profile(String profile) {
        Failure.require(
                PROFILE.equals(profile),
                "SECONDARY_NAVIGATION_CONTRACT",
                "Exact Free A11 7.3.1 secondary navigation profile required",
                3);
    }

    static void validate(String profile, int api, Path original, byte[] dex) throws Exception {
        profile(profile);
        Api30SecondaryNavigationPolicy.validate(api, original, dex);
    }

    static byte[] apply(String profile, int api, Path original, byte[] dex) throws Exception {
        profile(profile);

        return Api30SecondaryNavigationPolicy.apply(api, original, dex);
    }
}
