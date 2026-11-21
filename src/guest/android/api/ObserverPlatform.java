package com.voyemu.api;

/** API30 is enabled only by a source-built host binding that names the platform of its profile. */
final class ObserverPlatform {
    static final String QINGGAN11 = "qinggan11";
    static final String PASSION = "passion";

    static boolean allowed(int api, String platform) {
        return (api == 28 && platform.isEmpty())
                || (api == 30 && (QINGGAN11.equals(platform) || PASSION.equals(platform)));
    }

    static String provenance(int api, String prefix, String suffix) {
        if (api != 28 && api != 30) {
            throw new IllegalArgumentException("owned_api28_observer_required");
        }

        return prefix + "api" + api + "_" + suffix;
    }

    static String defaultScope(int api) {
        return provenance(api, "", "default_display");
    }
}
