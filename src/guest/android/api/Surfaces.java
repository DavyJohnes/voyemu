package com.voyemu.api;

/**
 * Observed surfaces by slot, named as the host names them: the primary and secondary multimedia
 * screens and the rear screen, which is the provider display with the rear unique ID.
 */
final class Surfaces {
    static final String[] SCREENS = {"primary", "secondary", "rear"};

    private Surfaces() {}

    /** The slot of a requested surface or screen name, -1 for an unknown one. */
    static int slot(String name) {
        for (int slot = 0; slot < SCREENS.length; ++slot) {
            if (SCREENS[slot].equals(name) || surface(slot).equals(name)) {
                return slot;
            }
        }

        return -1;
    }

    static String surface(int slot) {
        return "media_" + SCREENS[slot];
    }

    static boolean rear(String uniqueId) {
        return SecondaryDisplay.REAR.equals(uniqueId);
    }
}
