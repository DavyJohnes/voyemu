package com.voyemu.prepare;

/** Reuses the strict real JSON parser, rather than a flat regex transport fixture. */
public final class PhonePeerJsonBridge {
    public static Object parse(String text) {
        return Json.parse(text);
    }

    public static String write(Object value) {
        return Json.write(value);
    }
}
