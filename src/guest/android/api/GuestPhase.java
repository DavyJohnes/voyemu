package com.voyemu.api;

import java.util.Locale;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Closed numeric and WindowState-hash guest checkpoints; no request, window title, node or
 * credential data.
 */
final class GuestPhase {
    static final int MAX_REQUEST_EVENTS = 160, MAX_PROCESS_EVENTS = 4096;

    enum Phase {
        REQUEST_ENTERED,
        GATE_BEGIN,
        GATE_ACQUIRED,
        DISPLAY_RESOLVED,
        SERVER_START_BEGIN,
        SERVER_START_END,
        LIST_BEGIN,
        LIST_END,
        WM_BEGIN,
        WM_END,
        WINDOW_SELECTED,
        DUMP_BEGIN,
        DUMP_END,
        AX_INVENTORY_BEGIN,
        AX_INVENTORY_READY,
        AX_INVENTORY_MISSING,
        AX_REPAIR_BEGIN,
        AX_REPAIR_READY,
        COVERAGE_BEGIN,
        COVERAGE_VERIFIED,
        COVERAGE_NO_APPLICATION_ROOT,
        COVERAGE_UNCLASSIFIED_OWNER_REJECTED,
        IDENTITY_BEGIN,
        IDENTITY_END,
        RESPONSE_WRITE_BEGIN,
        RESPONSE_WRITE_END,
        DIAGNOSTIC_CAP
    }

    private static final AtomicLong SEQUENCE = new AtomicLong();
    private static final AtomicInteger TOTAL = new AtomicInteger();
    private static final ThreadLocal<State> CURRENT = new ThreadLocal<>();

    static final class State {
        final long sequence;
        final int pid;
        int events;

        State(long sequence, int pid) {
            this.sequence = sequence;
            this.pid = pid;
        }
    }

    static void begin(int pid) {
        CURRENT.set(new State(SEQUENCE.incrementAndGet(), pid));
    }

    static State state() {
        return CURRENT.get();
    }

    static void use(State state) {
        CURRENT.set(state);
    }

    static void end() {
        CURRENT.remove();
    }

    static String event(Phase phase, long uptime, int ordinal) {
        State state = CURRENT.get();

        if (state == null
                || state.pid <= 1
                || phase == null
                || uptime < 0
                || ordinal < -1
                || ordinal >= 64) {
            return "";
        }

        boolean window =
                phase == Phase.DUMP_BEGIN
                        || phase == Phase.DUMP_END
                        || phase == Phase.WINDOW_SELECTED;

        if (window != (ordinal >= 0)) {
            return "";
        }

        synchronized (state) {
            if (state.events >= MAX_REQUEST_EVENTS) {
                return "";
            }

            int reservation;

            do {
                reservation = TOTAL.get();

                if (reservation >= MAX_PROCESS_EVENTS) {
                    return "";
                }
            } while (!TOTAL.compareAndSet(reservation, reservation + 1));

            if (++state.events == MAX_REQUEST_EVENTS || reservation + 1 == MAX_PROCESS_EVENTS) {
                phase = Phase.DIAGNOSTIC_CAP;
                ordinal = -1;
            }
        }

        return "VOYEMU_UI_PHASE {\"seq\":"
                + state.sequence
                + ",\"pid\":"
                + state.pid
                + ",\"uptime_ms\":"
                + uptime
                + ",\"phase\":\""
                + phase.name().toLowerCase(Locale.ROOT)
                + "\",\"window_ordinal\":"
                + ordinal
                + "}";
    }

    static String windowEvent(
            long uptime,
            int ordinal,
            String hash,
            Integer display,
            Integer type,
            Integer ownerUid) {
        if (hash == null
                || !hash.matches("[a-f0-9]{1,8}")
                || (display != null && display < 0)
                || (type != null && (type < 0 || type > 9999))
                || (ownerUid != null && ownerUid < 0)) {
            return "";
        }

        String value = event(Phase.WINDOW_SELECTED, uptime, ordinal);

        if (value.isEmpty() || value.contains("\"phase\":\"diagnostic_cap\"")) {
            return value;
        }

        value =
                value.substring(0, value.length() - 1)
                        + ",\"window_hash\":\""
                        + hash
                        + "\",\"display_id\":"
                        + display
                        + ",\"window_type\":"
                        + type
                        + ",\"owner_uid\":"
                        + ownerUid
                        + "}";

        return value.length() <= 256 ? value : "";
    }

    static void markWindow(
            long uptime,
            int ordinal,
            String hash,
            Integer display,
            Integer type,
            Integer ownerUid) {
        try {
            String value = windowEvent(uptime, ordinal, hash, display, type, ownerUid);

            if (!value.isEmpty()) {
                System.err.println(value);
            }
        } catch (Exception ignored) {
            // checkstyle-allow IllegalCatch: diagnostics never alter an observation
            /* Diagnostics must not alter observation results. */
        }
    }

    static void mark(Phase phase, long uptime, int ordinal) {
        try {
            String event = event(phase, uptime, ordinal);

            if (!event.isEmpty()) {
                System.err.println(event);
            }
        } catch (Exception ignored) {
            // checkstyle-allow IllegalCatch: diagnostics never alter an observation
            /* Diagnostic output must not alter observation results. */
        }
    }
}
