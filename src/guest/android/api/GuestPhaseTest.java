package com.voyemu.api;

public final class GuestPhaseTest {
    private static void require(boolean value) {
        if (!value) {
            throw new AssertionError("guest phase contract");
        }
    }

    private static void rejectedWindowEvents() {
        for (String hash :
                new String[] {
                    "", "ABC", "123456789", "title", "abc\nsecret", "token=secret", "abc\""
                }) {
            require(GuestPhase.windowEvent(127, 1, hash, 1, 1, 10124).isEmpty());
        }

        require(GuestPhase.windowEvent(127, 1, null, 1, 1, 10124).isEmpty());
        require(GuestPhase.windowEvent(127, 1, "abc", -1, 1, 10124).isEmpty());
        require(GuestPhase.windowEvent(127, 1, "abc", 1, 10000, 10124).isEmpty());
        require(GuestPhase.windowEvent(127, 1, "abc", 1, -1, 10124).isEmpty());
        require(GuestPhase.windowEvent(127, 1, "abc", 1, 1, -1).isEmpty());
    }

    private static void readinessMarkers(GuestPhase.Phase[] readiness) {
        for (GuestPhase.Phase phase : readiness) {
            String marker = GuestPhase.event(phase, 128, -1);
            require(
                    marker.contains(
                            "\"phase\":\""
                                    + phase.name().toLowerCase(java.util.Locale.ROOT)
                                    + "\""));
            require(
                    !marker.contains("auth")
                            && !marker.contains("text")
                            && !marker.contains("package"));
            require(GuestPhase.event(phase, 128, 0).isEmpty());
        }
    }

    private static void rejectedEvents() throws Exception {
        for (int invalid : new int[] {-2, 64, Integer.MAX_VALUE}) {
            require(GuestPhase.event(GuestPhase.Phase.DUMP_BEGIN, 124, invalid).isEmpty());
        }

        require(GuestPhase.event(GuestPhase.Phase.DUMP_BEGIN, 124, -1).isEmpty());
        require(GuestPhase.event(GuestPhase.Phase.LIST_BEGIN, 124, 0).isEmpty());
        require(GuestPhase.event(null, 124, -1).isEmpty());
        require(GuestPhase.event(GuestPhase.Phase.LIST_BEGIN, -1, -1).isEmpty());
        final String[] foreign = new String[1];
        Thread thread =
                new Thread(() -> {
                    foreign[0] = GuestPhase.event(GuestPhase.Phase.WM_BEGIN, 125, -1);
                });
        thread.start();
        thread.join();
        require(foreign[0].isEmpty());
        Thread selectedThread =
                new Thread(() -> {
                    foreign[0] = GuestPhase.windowEvent(125, 1, "abc", 1, 1, 10124);
                });
        selectedThread.start();
        selectedThread.join();
        require(foreign[0].isEmpty());
    }

    private static int cappedWindowEvents() {
        GuestPhase.begin(4821);
        int selectedPerRequest = 0;
        String selectedLast = "";

        for (int i = 0; i < 200; ++i) {
            String value = GuestPhase.windowEvent(125, 1, "abc", 1, 1, 10124);

            if (!value.isEmpty()) {
                ++selectedPerRequest;
                selectedLast = value;
            }
        }

        require(
                selectedPerRequest == 160
                        && selectedLast.contains("\"phase\":\"diagnostic_cap\"")
                        && !selectedLast.contains("window_hash"));
        GuestPhase.end();

        return selectedPerRequest;
    }

    public static void main(String[] args) throws Exception {
        require(GuestPhase.event(GuestPhase.Phase.REQUEST_ENTERED, 1, -1).isEmpty());
        GuestPhase.begin(4821);
        String entered = GuestPhase.event(GuestPhase.Phase.REQUEST_ENTERED, 123, -1);
        require(
                entered.equals(
                        "VOYEMU_UI_PHASE"
                            + " {\"seq\":1,\"pid\":4821,\"uptime_ms\":123,\"phase\":\"request_entered\",\"window_ordinal\":-1}"));
        require(
                GuestPhase.event(GuestPhase.Phase.DUMP_BEGIN, 124, 63)
                        .contains("\"window_ordinal\":63"));
        String selected = GuestPhase.windowEvent(125, 1, "4ebc658", 1, 1, 10124);
        require(
                selected.contains("\"phase\":\"window_selected\"")
                        && selected.contains("\"window_hash\":\"4ebc658\"")
                        && selected.contains("\"display_id\":1")
                        && selected.contains("\"window_type\":1")
                        && selected.contains("\"owner_uid\":10124"));
        require(
                GuestPhase.windowEvent(126, 0, "d14522c", 1, null, null)
                        .contains("\"owner_uid\":null"));
        String maximum =
                GuestPhase.windowEvent(
                        Long.MAX_VALUE, 63, "ffffffff", Integer.MAX_VALUE, 9999, Integer.MAX_VALUE);
        require(!maximum.isEmpty() && maximum.length() <= 256);
        rejectedWindowEvents();
        GuestPhase.Phase[] readiness = {
            GuestPhase.Phase.AX_INVENTORY_BEGIN,
            GuestPhase.Phase.AX_INVENTORY_READY,
            GuestPhase.Phase.AX_INVENTORY_MISSING,
            GuestPhase.Phase.AX_REPAIR_BEGIN,
            GuestPhase.Phase.AX_REPAIR_READY,
            GuestPhase.Phase.COVERAGE_BEGIN,
            GuestPhase.Phase.COVERAGE_VERIFIED,
            GuestPhase.Phase.COVERAGE_NO_APPLICATION_ROOT,
            GuestPhase.Phase.COVERAGE_UNCLASSIFIED_OWNER_REJECTED
        };
        readinessMarkers(readiness);
        rejectedEvents();
        GuestPhase.end();
        require(GuestPhase.event(GuestPhase.Phase.RESPONSE_WRITE_BEGIN, 126, -1).isEmpty());
        int selectedPerRequest = cappedWindowEvents();
        GuestPhase.begin(4821);
        int perRequest = 0;
        String last = "";

        for (int i = 0; i < 1000; ++i) {
            String event = GuestPhase.event(GuestPhase.Phase.LIST_BEGIN, 1000 + i, -1);

            if (!event.isEmpty()) {
                ++perRequest;
                last = event;
            }
        }

        require(perRequest == 160 && last.contains("\"phase\":\"diagnostic_cap\""));
        GuestPhase.end();
        int process = 0, bytes = 0;

        for (int i = 0; i < 5000; ++i) {
            GuestPhase.begin(4821);
            String event = GuestPhase.event(GuestPhase.Phase.LIST_BEGIN, Long.MAX_VALUE, -1);
            GuestPhase.end();

            if (!event.isEmpty()) {
                ++process;
                bytes += event.length() + 1;
                last = event;
                require(event.length() <= 200);
            }
        }

        require(
                process + perRequest + selectedPerRequest + 5 + readiness.length == 4096
                        && bytes < 1024 * 1024
                        && last.contains("\"phase\":\"diagnostic_cap\""));
        GuestPhase.begin(4821);
        require(GuestPhase.event(GuestPhase.Phase.WM_BEGIN, 1, -1).isEmpty());
        GuestPhase.end();
        require(
                !selected.contains("title")
                        && !selected.contains("package")
                        && !selected.contains("text")
                        && !selected.contains("auth"));
        require(
                !entered.contains("auth")
                        && !entered.contains("title")
                        && !entered.contains("text"));
        System.out.println(
                "Guest phase numeric schema, thread isolation, ordinal and total/request caps"
                        + " passed");
    }
}
