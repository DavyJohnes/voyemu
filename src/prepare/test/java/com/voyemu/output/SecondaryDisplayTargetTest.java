package com.voyemu.output;

public final class SecondaryDisplayTargetTest {
    private static void check(boolean ok) {
        if (!ok) {
            throw new AssertionError("Secondary display contract");
        }
    }

    public static void main(String[] args) {
        check(
                SecondaryDisplayTarget.select(
                                new int[] {0, 1}, new String[] {"Built-in", "Emulator 2D Display"})
                        == 1);
        check(
                SecondaryDisplayTarget.select(
                                new int[] {1, 2},
                                new String[] {"Emulator2D", "Emulator 2D Display"})
                        == -1);
        check(
                SecondaryDisplayTarget.select(
                                new int[] {1}, new String[] {"Emulator 2D Display extra"})
                        == -1);
        check(
                SecondaryDisplayTarget.select(
                                new int[] {0, 17}, new String[] {"Built-in", "Emulator2D"})
                        == 17);
        check(
                SecondaryDisplayTarget.select(
                                new int[] {29, 0}, new String[] {"Emulator2D", "Built-in"})
                        == 29);
        check(SecondaryDisplayTarget.select(new int[] {0}, new String[] {"Emulator2D"}) == -1);
        check(
                SecondaryDisplayTarget.select(
                                new int[] {2, 3}, new String[] {"Emulator2D", "Emulator2D"})
                        == -1);
        check(SecondaryDisplayTarget.select(new int[] {1}, new String[] {"Unknown"}) == -1);
        check(SecondaryDisplayTarget.select(new int[] {1}, new String[0]) == -1);
        check(SecondaryDisplayTarget.select(null, new String[0]) == -1);
        check(SecondaryDisplayTarget.select(new int[65], new String[65]) == -1);
        check(SecondaryDisplayTarget.PACKAGE.equals("com.voyah.os.carlauncher"));
        check(
                SecondaryDisplayTarget.ACTIVITY.equals(
                        "com.voyah.os.carlauncher.second.LauncherActivityMainSecond"));
        System.out.println(
                "Dynamic secondary target selection guards passed; no guest readiness claim");
    }
}
