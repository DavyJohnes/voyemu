package com.voyemu.api;

public final class WindowReadDiagnosticsTest {
    private static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    public static void main(String[] args) {
        check(
                WindowReadDiagnostics.fields(new java.net.SocketTimeoutException("private"), 7)
                        .get("reason")
                        .equals("socket_timeout"));
        check(
                WindowReadDiagnostics.fields(new IllegalArgumentException("dump_incomplete"), 0)
                        .get("reason")
                        .equals("dump_incomplete"));
        check(
                WindowReadDiagnostics.fields(new IllegalStateException("deadline"), 5)
                        .get("reason")
                        .equals("deadline"));
        check(
                WindowReadDiagnostics.fields(new java.io.IOException("private"), -1)
                        .get("elapsed_ms")
                        .equals(0L));
        check(
                WindowReadDiagnostics.fields(new IllegalArgumentException("private-secret"), 1)
                        .get("reason")
                        .equals("unclassified"));
        check(
                !WindowReadDiagnostics.fields(new RuntimeException("private-secret"), 2)
                        .toString()
                        .contains("private-secret"));
        check(WindowReadDiagnostics.dumpShape("").get("empty").equals(true));
        check(WindowReadDiagnostics.dumpShape("DONE.\n").get("done_only").equals(true));
        check(
                WindowReadDiagnostics.dumpShape("Private@abc text=6,secret \nDONE.\n")
                        .get("terminal_done")
                        .equals(true));
        check(
                !WindowReadDiagnostics.dumpShape("Private@abc text=6,secret \nDONE.\n")
                        .toString()
                        .contains("secret"));
        check(WindowReadDiagnostics.dumpShape("x\n").get("newlines").equals(1));
        check(
                "wallpaper_surface_no_view_hierarchy"
                        .equals(
                                WindowReadDiagnostics.unsupportedReason(
                                        2013, "com.android.systemui.ImageWallpaper")));
        check(
                WindowReadDiagnostics.unsupportedReason(null, "com.android.systemui.ImageWallpaper")
                        == null);
        check(
                WindowReadDiagnostics.unsupportedReason(1, "com.android.systemui.ImageWallpaper")
                        == null);
        check(WindowReadDiagnostics.unsupportedReason(2013, "other.Wallpaper") == null);
        check(WindowReadDiagnostics.unsupportedReason(2013, null) == null);
        check(
                WindowReadDiagnostics.fields(
                                new IllegalStateException("wallpaper_surface_no_view_hierarchy"), 0)
                        .get("reason")
                        .equals("wallpaper_surface_no_view_hierarchy"));
        System.out.println("window diagnostics tests passed");
    }
}
