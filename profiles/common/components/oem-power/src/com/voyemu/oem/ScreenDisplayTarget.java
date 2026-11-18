package com.voyemu.oem;

/** OEM screen 1 is SDK default; screen 2 is the unique SDK passenger display. */
final class ScreenDisplayTarget {
    static boolean matches(int screen, int id, String name, int width, int height) {
        if (width != 1920 || height != 720) {
            return false;
        }

        if (screen == 1) {
            return id == 0;
        }

        return screen == 2
                && id > 0
                && ("Emulator2D".equals(name) || "Emulator 2D Display".equals(name));
    }

    static int select(int screen, int[] ids, String[] names, int[] widths, int[] heights) {
        if (ids.length != names.length
                || ids.length != widths.length
                || ids.length != heights.length) {
            return -1;
        }

        int selected = -1;

        for (int i = 0; i < ids.length; i++) {
            if (matches(screen, ids[i], names[i], widths[i], heights[i])) {
                if (selected != -1) {
                    return -1;
                }

                selected = i;
            }
        }

        return selected;
    }

    private ScreenDisplayTarget() {}
}
