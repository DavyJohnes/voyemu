package com.qinggan.os;

public class SystemProperties {
    public static String value;
    public static boolean fail;
    public static int calls;

    public static String getVinCode() {
        calls++;

        if (fail) {
            throw new IllegalStateException();
        }

        return value;
    }
}
