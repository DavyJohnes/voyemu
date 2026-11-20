package com.voyemu.prepare;

import java.nio.file.Path;

/**
 * Passion binding: retain stock navigation suppression and enable the canonical decorated secondary
 * status bar.
 */
final class PassionSecondaryNavigationGuard {
    static final String NAME = "passion-api30-secondary-navigation";
    static final String PIN = Api30SecondaryNavigationPolicy.PIN;

    static void validate(String profile, int api, Path original, byte[] dex) throws Exception {
        Api30SecondaryNavigationPolicy.validate(api, original, dex);
    }

    static byte[] apply(String profile, int api, Path original, byte[] dex) throws Exception {
        return Api30SecondaryNavigationPolicy.apply(api, original, dex, true);
    }
}
