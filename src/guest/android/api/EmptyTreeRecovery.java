package com.voyemu.api;

/**
 * One reconnect for an empty or application-incomplete tree; no incomplete observation is accepted.
 */
final class EmptyTreeRecovery {
    static boolean unavailableAccessibilityWindow(Object identity, int windowId) {
        if (identity instanceof Integer) {
            return ((Integer) identity).intValue() == windowId;
        }

        if (identity instanceof Long) {
            long value = ((Long) identity).longValue();

            if (value >= Integer.MIN_VALUE && value <= Integer.MAX_VALUE) {
                return value == windowId;
            }
        }

        // WindowState hashes and AX integer IDs are distinct namespaces, including digit-only
        // hashes.
        if (identity instanceof String && ((String) identity).matches("[a-fA-F0-9]{1,8}")) {
            return false;
        }

        throw new IllegalStateException("window_display_conflict");
    }

    static boolean missingInteractivePanel(String basis) {
        return "unresolved_displayed_navigation_panel".equals(basis);
    }

    static boolean focusedApplicationMissing(
            int type, boolean active, boolean focused, boolean missing) {
        return type == 1 && active && focused && missing;
    }

    static final class InventoryUnavailable extends IllegalStateException {
        InventoryUnavailable() {
            super("application_window_unverified");
        }
    }

    interface Clock {
        long now();
    }

    interface Pause {
        void run(long millis) throws Exception;
    }

    interface Read<T> {
        T run() throws Exception;
    }

    interface Repair {
        void run() throws Exception;
    }

    interface Incomplete<T> {
        boolean test(T value) throws Exception;
    }

    static <T> T read(Read<T> observation, Repair repair) throws Exception {
        return read(observation, repair, value -> false);
    }

    static <T> T read(Read<T> observation, Repair repair, Incomplete<T> incomplete)
            throws Exception {
        return read(observation, repair, incomplete, null, null, 0);
    }

    static <T> T read(
            Read<T> observation,
            Repair repair,
            Incomplete<T> incomplete,
            Clock clock,
            Pause pause,
            long deadline)
            throws Exception {
        T value;

        try {
            value = observation.run();
        } catch (IllegalStateException error) {
            if (!"empty_tree_unverified".equals(error.getMessage())
                    && !"application_window_unverified".equals(error.getMessage())) {
                throw error;
            }

            repair.run();

            return afterRepair(observation, clock, pause, deadline);
        }

        if (!incomplete.test(value)) {
            return value;
        }

        repair.run();

        return afterRepair(observation, clock, pause, deadline);
    }

    private static <T> T afterRepair(Read<T> observation, Clock clock, Pause pause, long deadline)
            throws Exception {
        long limit = clock == null ? 0 : Math.min(deadline, clock.now() + 250);

        for (; ; ) {
            try {
                return observation.run();
            } catch (InventoryUnavailable missing) {
                long remaining = clock == null ? 0 : limit - clock.now();

                if (remaining <= 0) {
                    throw missing;
                }

                pause.run(Math.min(10, remaining));
            }
        }
    }
}
