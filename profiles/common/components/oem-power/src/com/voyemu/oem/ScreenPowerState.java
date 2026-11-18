package com.voyemu.oem;

/** Independent OEM screen status, shared between Binder reads and main-thread windows. */
final class ScreenPowerState {
    private final boolean[] off = new boolean[2];

    static boolean supports(int screen) {
        return screen == 1 || screen == 2;
    }

    synchronized boolean isOff(int screen) {
        return supports(screen) && off[screen - 1];
    }

    synchronized void setOff(int screen, boolean value) {
        if (!supports(screen)) {
            throw new IllegalArgumentException("Unknown OEM screen");
        }

        off[screen - 1] = value;
    }

    synchronized int[] status() {
        return new int[] {off[0] ? 5 : 1, off[1] ? 5 : 1};
    }
}
