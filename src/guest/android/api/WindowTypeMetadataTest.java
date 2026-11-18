package com.voyemu.api;

public final class WindowTypeMetadataTest {
    public static void main(String[] args) {
        if (!Integer.valueOf(2010)
                .equals(WindowTypeMetadata.parse(" mAttrs={(0,0)(fillxfill) ty=SYSTEM_ERROR"))) {
            throw new AssertionError("actual OEM Phone type missing");
        }

        if (!Integer.valueOf(2106)
                .equals(
                        WindowTypeMetadata.parse(
                                " mAttrs={(0,0)(fillxfill) sim=#20 ty=2106 fl=#0}"))) {
            throw new AssertionError();
        }

        if (!Integer.valueOf(1).equals(WindowTypeMetadata.parse(" mAttrs={ty=1}"))) {
            throw new AssertionError();
        }

        if (!Integer.valueOf(2013)
                .equals(
                        WindowTypeMetadata.parse(
                                " mAttrs={(0,0)(1920x720) ty=WALLPAPER fmt=RGBX_8888"))) {
            throw new AssertionError();
        }

        for (String invalid :
                new String[] {
                    "type=2106",
                    "ty=unknown",
                    "ty=-1",
                    "ty=12345678901",
                    "empty=0",
                    "ty=SYSTEM_ERROR_FAKE",
                    "ty=SYSTEM_ERROR1",
                    "ty=NAVIGATION_BAR_PANEL_FAKE",
                    "ty=NAVIGATION_BAR_PANEL1",
                    "ty=WALLPAPER_FAKE",
                    "ty=WALLPAPER1",
                    "ty=2013FAKE"
                }) {
            if (WindowTypeMetadata.parse(invalid) != null) {
                throw new AssertionError("invented WM type");
            }
        }

        System.out.println("WM numeric vendor type and unknown metadata tests passed");
    }
}
