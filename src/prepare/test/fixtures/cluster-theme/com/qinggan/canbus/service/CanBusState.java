package com.qinggan.canbus.service;

/** Only the property reader of the component state; the theme tests run without properties. */
final class CanBusState {
    private CanBusState() {}

    static String systemProp(String key, String def) {
        return def;
    }
}
