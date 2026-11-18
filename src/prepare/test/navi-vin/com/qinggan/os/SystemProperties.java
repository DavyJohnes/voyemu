package com.qinggan.os;

public class SystemProperties {
    public static String vin;
    public static int calls;
    public static boolean fail;

    public static String getVinCode() {
        calls++;

        if (fail) {
            throw new IllegalStateException();
        }

        return vin;
    }
}
