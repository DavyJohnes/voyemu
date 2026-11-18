package com.voyemu.api;

public final class SecondaryDisplayTest {
    public static void main(String[] args) {
        accepted();
        rejected();
    }

    private static void accepted() {
        if (!SecondaryDisplay.matches(30, 1, "Emulator 2D Display")) {
            throw new AssertionError("API30 real spaced display unavailable");
        }

        if (!SecondaryDisplay.matches(
                        30,
                        1,
                        "Emulator 2D Display",
                        "virtual:com.android.emulator.multidisplay:1234562")
                || SecondaryDisplay.matches(30, 2, "Emulator 2D Display", SecondaryDisplay.REAR)) {
            throw new AssertionError("the rear screen is not the secondary display");
        }

        if (!SecondaryDisplay.matches(30, 19, "Emulator 2D Display")
                || !SecondaryDisplay.matches(30, 19, "Emulator2D")
                || !SecondaryDisplay.matches(28, 4, "Emulator2D")) {
            throw new AssertionError("observed provider name or nonconstant ID rejected");
        }

        // Preserve the previous API28 contains predicate, including decorated compact names.
        for (String name : new String[] {"Emulator2D Extra", "FakeEmulator2D", "Emulator2D\n"}) {
            if (!SecondaryDisplay.matches(28, 4, name)) {
                throw new AssertionError("legacy API28 contains behavior changed");
            }
        }

        if (!SecondaryDisplay.matches(28, -1, "Emulator2D")) {
            throw new AssertionError("legacy API28 nonprimary predicate changed");
        }
    }

    private static void rejected() {
        for (String name :
                new String[] {
                    null,
                    "",
                    "Built-in Screen",
                    "FakeEmulator2D",
                    "Emulator2D Extra",
                    "Emulator 2D Display Extra",
                    " Emulator 2D Display",
                    "Emulator 2D Display\n"
                }) {
            if (SecondaryDisplay.matches(30, 1, name)) {
                throw new AssertionError("unobserved API30 provider accepted");
            }
        }

        for (String name : new String[] {"Emulator2D", "Emulator 2D Display"}) {
            if (SecondaryDisplay.matches(30, 0, name)
                    || SecondaryDisplay.matches(30, -1, name)
                    || SecondaryDisplay.matches(28, 0, name)) {
                throw new AssertionError("default or invalid API30 display selected as secondary");
            }
        }

        for (String name : new String[] {null, "", "Built-in Screen", "Emulator 2D Display"}) {
            if (SecondaryDisplay.matches(28, 4, name)) {
                throw new AssertionError("legacy API28 predicate widened");
            }
        }

        if (SecondaryDisplay.matches(29, 1, "Emulator2D")
                || SecondaryDisplay.matches(31, 1, "Emulator2D")
                || SecondaryDisplay.matches(29, 1, "Emulator 2D Display")
                || SecondaryDisplay.matches(31, 1, "Emulator 2D Display")) {
            throw new AssertionError("API30 compatibility escaped version scope");
        }

        System.out.println(
                "Legacy API28 contains preserved; exact API30 secondary provider and negative"
                        + " identities passed");
    }
}
