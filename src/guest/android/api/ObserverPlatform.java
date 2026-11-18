package com.voyemu.api;

/** API30 is enabled only by a source-built, exact-profile host binding. */
final class ObserverPlatform {
    static final String QINGGAN11_611 = "free-11-chinese-20250710-6.11.1";
    static final String QINGGAN11_731 = "free-11-chinese-20260731-7.3.1";
    static final String PASSION = "passion-11-russian-20260403-2.1.4.80";

    static boolean allowed(int api, String profile) {
        return (api == 28 && profile.isEmpty())
                || (api == 30
                        && (QINGGAN11_611.equals(profile)
                                || QINGGAN11_731.equals(profile)
                                || PASSION.equals(profile)));
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
