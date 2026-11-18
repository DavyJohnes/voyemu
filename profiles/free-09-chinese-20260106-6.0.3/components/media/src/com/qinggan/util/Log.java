package com.qinggan.util;

// Minimal stub of the OEM com.qinggan.util.Log wrapper (only d/e/i used by
// the copied canbus parcelables).
public class Log {
    public static int d(String tag, String msg) {
        return android.util.Log.d(tag, msg);
    }

    public static int e(String tag, String msg) {
        return android.util.Log.e(tag, msg);
    }

    public static int i(String tag, String msg) {
        return android.util.Log.i(tag, msg);
    }

    public static int e(String tag, String msg, Throwable t) {
        return android.util.Log.e(tag, msg, t);
    }
}
