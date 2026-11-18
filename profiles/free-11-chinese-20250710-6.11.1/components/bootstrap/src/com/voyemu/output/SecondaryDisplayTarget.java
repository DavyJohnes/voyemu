package com.voyemu.output;

/** Exact SDK provider identities; dimensions and a unique non-default ID are required. */
final class SecondaryDisplayTarget {
    static int select(int[] ids, String[] names, int[] widths, int[] heights) {
        if (ids == null
                || names == null
                || widths == null
                || heights == null
                || ids.length != names.length
                || ids.length != widths.length
                || ids.length != heights.length
                || ids.length > 64) {
            return -1;
        }

        int selected = -1;

        for (int i = 0; i < ids.length; i++) {
            boolean known = "Emulator2D".equals(names[i]) || "Emulator 2D Display".equals(names[i]);

            if (ids[i] <= 0 || !known || widths[i] != 1920 || heights[i] != 720) {
                continue;
            }

            if (selected != -1) {
                return -1;
            }

            selected = ids[i];
        }

        return selected;
    }

    private SecondaryDisplayTarget() {}
}
