package com.qinggan.os;

final class BackLightNative {
    static void load(String file) {
        System.load(file);
    }

    static native int native_backlight_get_stm8();

    static native int native_backlight_set_stm8(int value);

    static native int native_close_backlight();

    static native int native_close_backlight_for_screen(int screen);

    static native int native_close_brightness();

    static native int native_get_backlight();

    static native int native_get_backlight_for_screen(int screen);

    static native String native_get_backlight_ver();

    static native int native_get_key_backlight();

    static native int native_get_key_state();

    static native int native_get_mode(int screen);

    static native int native_get_screen_state();

    static native int native_get_screen_state_for_screen(int screen);

    static native int native_get_stm8_state();

    static native int native_open_backlight();

    static native int native_open_backlight_for_screen(int screen);

    static native int native_open_brightness();

    static native int native_set_backlight(int value);

    static native int native_set_backlight_for_screen(int value, int screen);

    static native int native_set_key_state(int value);

    static native int native_set_keypad_backlight(int value);

    static native int native_set_mode(int value, int screen);

    static native int native_set_stm8_state(int value);

    static native int native_slidebar_set_state(int value);
}
