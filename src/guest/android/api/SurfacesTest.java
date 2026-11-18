package com.voyemu.api;

public final class SurfacesTest {
    public static void main(String[] args) {
        if (Surfaces.slot("media_primary") != 0
                || Surfaces.slot("secondary") != 1
                || Surfaces.slot("media_rear") != 2
                || Surfaces.slot("rear") != 2) {
            throw new AssertionError("surface names map to their slots");
        }

        if (Surfaces.slot("hud") != -1
                || Surfaces.slot("media_hud") != -1
                || Surfaces.slot(null) != -1) {
            throw new AssertionError("only the Android screens are surfaces");
        }

        if (!Surfaces.surface(2).equals("media_rear")
                || !Surfaces.rear("virtual:com.android.emulator.multidisplay:1234563")
                || Surfaces.rear("virtual:com.android.emulator.multidisplay:1234562")) {
            throw new AssertionError("the rear surface is the rear provider display");
        }

        System.out.println("surface slots and the rear display identity tests passed");
    }
}
