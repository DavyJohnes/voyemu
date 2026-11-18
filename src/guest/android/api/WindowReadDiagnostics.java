package com.voyemu.api;

/** Fixed diagnostics only: never export exception messages or source text. */
final class WindowReadDiagnostics {
    static String unsupportedReason(Integer windowType, String title) {
        // Android 9 WallpaperService uses BaseIWindow.executeCommand, an empty
        // implementation rather than ViewRootImpl debug dispatch. Keep the
        // exact stock component and WM wallpaper type fence; unknown windows
        // still receive the ordinary bounded DUMP attempt.
        return Integer.valueOf(2013).equals(windowType)
                        && "com.android.systemui.ImageWallpaper".equals(title)
                ? "wallpaper_surface_no_view_hierarchy"
                : null;
    }

    static java.util.Map<String, Object> dumpShape(String dump) {
        java.util.Map<String, Object> fields = new java.util.LinkedHashMap<>();

        fields.put("utf16_units", dump.length());
        int lines = 0;

        for (int i = 0; i < dump.length(); ++i) {
            if (dump.charAt(i) == '\n') {
                ++lines;
            }
        }

        fields.put("newlines", lines);
        fields.put("empty", dump.isEmpty());
        fields.put("done_only", dump.trim().equals("DONE."));
        fields.put("terminal_done", dump.trim().endsWith("DONE."));

        return fields;
    }

    static java.util.Map<String, Object> fields(Exception error, long elapsed) {
        String reason = "unclassified", type = "other";

        if (error instanceof java.net.SocketTimeoutException) {
            reason = "socket_timeout";
            type = "socket_timeout";
        } else if (error instanceof java.nio.charset.CharacterCodingException) {
            reason = "invalid_utf8";
            type = "character_coding";
        } else if (error instanceof java.io.IOException) {
            reason = "io_failure";
            type = "io";
        } else if (error instanceof IllegalArgumentException
                || error instanceof IllegalStateException) {
            type = error instanceof IllegalArgumentException ? "illegal_argument" : "illegal_state";
            String message = error.getMessage();

            for (String code :
                    new String[] {
                        "deadline",
                        "dump_limit",
                        "dump_depth",
                        "dump_identity",
                        "dump_duplicate_root_or_identity",
                        "dump_property",
                        "dump_length",
                        "dump_duplicate_property",
                        "dump_incomplete",
                        "node_limit",
                        "viewserver_debug_gate",
                        "viewserver_owner_conflict",
                        "viewserver_start_failed",
                        "window_identity",
                        "wallpaper_surface_no_view_hierarchy"
                    }) {
                if (code.equals(message)) {
                    reason = code;
                    break;
                }
            }
        }

        java.util.Map<String, Object> fields = new java.util.LinkedHashMap<>();
        fields.put("reason", reason);
        fields.put("exception_type", type);
        fields.put("elapsed_ms", Math.max(0, elapsed));

        return fields;
    }
}
