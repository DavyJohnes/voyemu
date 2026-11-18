package com.qinggan.util;

/** Host-independent Android log forwarding for the NXP media ABI. */
public final class Log {
    private Log() {}

    public static void d(String t, String m) {
        android.util.Log.d(t, m);
    }

    public static void e(String t, String m) {
        android.util.Log.e(t, m);
    }

    public static void i(String t, String m) {
        android.util.Log.i(t, m);
    }

    public static void w(String t, String m) {
        android.util.Log.w(t, m);
    }
}
