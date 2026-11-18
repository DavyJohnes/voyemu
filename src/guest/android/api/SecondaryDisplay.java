package com.voyemu.api;

/**
 * Keep the legacy API28 provider predicate; API30 admits exact observed names only. The rear screen
 * is a provider display too and never the secondary one.
 */
final class SecondaryDisplay {
    static final String REAR = "virtual:com.android.emulator.multidisplay:1234563";

    static boolean matches(int api, int id, String name, String uniqueId) {
        return matches(api, id, name) && !REAR.equals(uniqueId);
    }

    static boolean matches(int api, int id, String name) {
        if (api == 28) {
            return id != 0 && name != null && name.contains("Emulator2D");
        }

        return api == 30
                && id > 0
                && ("Emulator2D".equals(name) || "Emulator 2D Display".equals(name));
    }
}
