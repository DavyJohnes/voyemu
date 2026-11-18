package com.voyemu.api;

public final class EmptyTreeRecoveryTest {
    private static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    public static void main(String[] args) throws Exception {
        classification();
        repairs();
        focusedApplication();
        partialRepairs();
        readiness();
    }

    private static void classification() throws Exception {
        check(!EmptyTreeRecovery.unavailableAccessibilityWindow("d0c317d", 17));
        check(!EmptyTreeRecovery.unavailableAccessibilityWindow("17", 17));
        check(EmptyTreeRecovery.unavailableAccessibilityWindow(Integer.valueOf(17), 17));
        check(EmptyTreeRecovery.unavailableAccessibilityWindow(Long.valueOf(17), 17));
        check(!EmptyTreeRecovery.unavailableAccessibilityWindow(Integer.valueOf(18), 17));

        for (Object invalid :
                new Object[] {
                    null,
                    true,
                    Double.valueOf(17),
                    Long.valueOf(Long.MAX_VALUE),
                    "",
                    "not-a-hash",
                    "123456789",
                    new Object()
                }) {
            try {
                EmptyTreeRecovery.unavailableAccessibilityWindow(invalid, 17);
                throw new AssertionError("invalid unavailable ID accepted");
            } catch (IllegalStateException expected) {
                check("window_display_conflict".equals(expected.getMessage()));
            }
        }

        Object[] mixed = {"d0c317d", Integer.valueOf(17)};
        boolean mixedMissing = false;

        for (Object identity : mixed) {
            mixedMissing |= EmptyTreeRecovery.unavailableAccessibilityWindow(identity, 17);
        }

        check(mixedMissing);
        check(EmptyTreeRecovery.missingInteractivePanel("unresolved_displayed_navigation_panel"));

        for (String basis :
                new String[] {
                    null, "", "accessibility_application_root", "verified_navigation_panel"
                }) {
            check(!EmptyTreeRecovery.missingInteractivePanel(basis));
        }
    }

    private static void repairs() throws Exception {
        int[] reads = {0}, repairs = {0};
        String value =
                EmptyTreeRecovery.read(
                        () -> {
                            reads[0]++;

                            return "valid";
                        },
                        () -> repairs[0]++);

        check(value.equals("valid") && reads[0] == 1 && repairs[0] == 0);
        reads[0] = 0;
        value =
                EmptyTreeRecovery.read(
                        () -> {
                            if (++reads[0] == 1) {
                                throw new IllegalStateException("empty_tree_unverified");
                            }

                            return "fresh";
                        },
                        () -> repairs[0]++);
        check(value.equals("fresh") && reads[0] == 2 && repairs[0] == 1);
        reads[0] = 0;
        repairs[0] = 0;

        try {
            EmptyTreeRecovery.read(
                    () -> {
                        reads[0]++;
                        throw new IllegalStateException("empty_tree_unverified");
                    },
                    () -> repairs[0]++);
            throw new AssertionError("persistent empty tree accepted");
        } catch (IllegalStateException expected) {
            check(expected.getMessage().equals("empty_tree_unverified"));
        }

        check(reads[0] == 2 && repairs[0] == 1);

        for (String reason :
                new String[] {"deadline", "boot_changed", "stale_node", "request_owner"}) {
            reads[0] = 0;
            repairs[0] = 0;

            try {
                EmptyTreeRecovery.read(
                        () -> {
                            reads[0]++;
                            throw new IllegalStateException(reason);
                        },
                        () -> repairs[0]++);
                throw new AssertionError();
            } catch (IllegalStateException expected) {
                check(expected.getMessage().equals(reason));
            }

            check(reads[0] == 1 && repairs[0] == 0);
        }

        for (String reason : new String[] {"boot_changed", "deadline", "accessibility_service"}) {
            reads[0] = 0;

            try {
                EmptyTreeRecovery.read(
                        () -> {
                            reads[0]++;
                            throw new IllegalStateException("empty_tree_unverified");
                        },
                        () -> {
                            throw new IllegalStateException(reason);
                        });
                throw new AssertionError();
            } catch (IllegalStateException expected) {
                check(expected.getMessage().equals(reason));
            }

            check(reads[0] == 1);
        }
    }

    private static void focusedApplication() {
        for (int type : new int[] {-1, 1, 3}) {
            for (boolean active : new boolean[] {false, true}) {
                for (boolean focused : new boolean[] {false, true}) {
                    for (boolean missing : new boolean[] {false, true}) {
                        check(
                                EmptyTreeRecovery.focusedApplicationMissing(
                                                type, active, focused, missing)
                                        == (type == 1 && active && focused && missing));
                    }
                }
            }
        }
    }

    private static void partialRepairs() throws Exception {
        int[] reads = {0}, repairs = {0};
        String value =
                EmptyTreeRecovery.read(
                        () -> {
                            reads[0]++;

                            return "partial";
                        },
                        () -> repairs[0]++,
                        v -> true);

        check(value.equals("partial") && reads[0] == 2 && repairs[0] == 1);
        reads[0] = 0;
        repairs[0] = 0;
        value =
                EmptyTreeRecovery.read(
                        () -> {
                            reads[0]++;

                            return "bars_partial";
                        },
                        () -> repairs[0]++,
                        v -> false);
        check(value.equals("bars_partial") && reads[0] == 1 && repairs[0] == 0);
        reads[0] = 0;
        repairs[0] = 0;

        try {
            EmptyTreeRecovery.read(
                    () -> {
                        if (++reads[0] == 1) {
                            return "partial";
                        }

                        throw new IllegalStateException("deadline");
                    },
                    () -> repairs[0]++,
                    v -> true);
            throw new AssertionError();
        } catch (IllegalStateException expected) {
            check("deadline".equals(expected.getMessage()));
        }

        check(reads[0] == 2 && repairs[0] == 1);
        reads[0] = 0;
        repairs[0] = 0;
        value =
                EmptyTreeRecovery.read(
                        () ->
                                ++reads[0] == 1
                                        ? "unresolved_displayed_navigation_panel"
                                        : "verified_navigation_panel",
                        () -> repairs[0]++,
                        EmptyTreeRecovery::missingInteractivePanel);
        check(value.equals("verified_navigation_panel") && reads[0] == 2 && repairs[0] == 1);
    }

    private static void readiness() throws Exception {
        // partial -> reconnect -> missing selected display -> genuine verified observation.
        long[] now = {20089};
        int[] attempts = {0}, repaired = {0}, sleeps = {0};
        String ready =
                EmptyTreeRecovery.read(
                        () -> {
                            int attempt = ++attempts[0];

                            if (attempt == 1) {
                                return "partial";
                            }

                            if (attempt == 2) {
                                throw new EmptyTreeRecovery.InventoryUnavailable();
                            }

                            return "verified";
                        },
                        () -> repaired[0]++,
                        "partial"::equals,
                        () -> now[0],
                        millis -> {
                            check(millis > 0 && millis <= 10);
                            now[0] += millis;
                            sleeps[0]++;
                        },
                        22000);

        check("verified".equals(ready) && attempts[0] == 3 && repaired[0] == 1 && sleeps[0] == 1);

        for (long budget : new long[] {0, 7, 1000}) {
            now[0] = 100;
            attempts[0] = 0;
            repaired[0] = 0;

            try {
                EmptyTreeRecovery.read(
                        () -> {
                            if (++attempts[0] == 1) {
                                return "partial";
                            }

                            throw new EmptyTreeRecovery.InventoryUnavailable();
                        },
                        () -> repaired[0]++,
                        "partial"::equals,
                        () -> now[0],
                        millis -> now[0] += millis,
                        100 + budget);
                throw new AssertionError("permanent missing inventory accepted");
            } catch (EmptyTreeRecovery.InventoryUnavailable expected) {
                check("application_window_unverified".equals(expected.getMessage()));
            }

            check(now[0] == 100 + Math.min(budget, 250) && repaired[0] == 1);
            check(attempts[0] <= 27);
        }

        for (String reason :
                new String[] {
                    "application_window_unverified",
                    "window_changed",
                    "window_display_conflict",
                    "boot_changed",
                    "deadline"
                }) {
            now[0] = 0;
            attempts[0] = 0;

            try {
                EmptyTreeRecovery.read(
                        () -> {
                            if (++attempts[0] == 1) {
                                return "partial";
                            }

                            throw new IllegalStateException(reason);
                        },
                        () -> {},
                        "partial"::equals,
                        () -> now[0],
                        millis -> {
                            throw new AssertionError("non-inventory failure retried");
                        },
                        1000);
                throw new AssertionError("failure swallowed");
            } catch (IllegalStateException expected) {
                check(reason.equals(expected.getMessage()));
            }

            check(attempts[0] == 2);
        }

        // Without the post-repair bounded policy (legacy/API28), a missing inventory remains
        // immediate.
        attempts[0] = 0;

        try {
            EmptyTreeRecovery.read(
                    () -> {
                        if (++attempts[0] == 1) {
                            return "partial";
                        }

                        throw new EmptyTreeRecovery.InventoryUnavailable();
                    },
                    () -> {},
                    "partial"::equals);
            throw new AssertionError("legacy behavior changed");
        } catch (EmptyTreeRecovery.InventoryUnavailable expected) {
            check(attempts[0] == 2);
        }

        System.out.println("empty tree bounded recovery tests passed");
    }
}
