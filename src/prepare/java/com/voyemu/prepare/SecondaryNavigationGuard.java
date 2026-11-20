package com.voyemu.prepare;

import java.nio.file.Path;

/** Preserve the existing Free recipe and exact output while sharing the clean platform policy. */
final class SecondaryNavigationGuard {
    static final String NAME = "api30-secondary-navigation";
    static final String PIN = Api30SecondaryNavigationPolicy.PIN,
            TYPE = Api30SecondaryNavigationPolicy.TYPE;

    static void validate(String profile, int api, Path original, byte[] dex) throws Exception {
        Api30SecondaryNavigationPolicy.validate(api, original, dex);
    }

    static byte[] apply(String profile, int api, Path original, byte[] dex) throws Exception {
        return Api30SecondaryNavigationPolicy.apply(api, original, dex);
    }
}
