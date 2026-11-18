package com.voyemu.output;

/** Finite provider-name resolution; display IDs may change after a provider restart. */
final class SecondaryDisplayTarget {
    static final String NAME = "Emulator2D";
    static final String CURRENT_SDK_NAME = "Emulator 2D Display";
    static final String PACKAGE = "com.voyah.os.carlauncher";
    static final String ACTIVITY = "com.voyah.os.carlauncher.second.LauncherActivityMainSecond";

    static int select(int[] ids, String[] names) {
        if (ids == null || names == null || ids.length != names.length || ids.length > 64) {
            return -1;
        }

        int selected = -1;

        for (int i = 0; i < ids.length; i++) {
            if (ids[i] > 0 && (NAME.equals(names[i]) || CURRENT_SDK_NAME.equals(names[i]))) {
                if (selected != -1) {
                    return -1;
                }

                selected = ids[i];
            }
        }

        return selected;
    }

    private SecondaryDisplayTarget() {}
}
