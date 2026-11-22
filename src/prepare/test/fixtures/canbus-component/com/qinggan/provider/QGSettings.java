package com.qinggan.provider;

import android.content.ContentResolver;

import java.util.HashMap;
import java.util.Map;

/** In-memory OEM QGSettings tables. */
public final class QGSettings {
    // The fixture mirrors this nested type name from the OEM provider API.
    @SuppressWarnings("AvoidCommonTypeNames")
    public static final class System {
        public static final Map<String, Integer> VALUES = new HashMap<>();

        public static synchronized int getInt(ContentResolver resolver, String key, int value) {
            return VALUES.getOrDefault(key, value);
        }

        public static synchronized boolean putInt(ContentResolver resolver, String key, int value) {
            VALUES.put(key, value);

            return true;
        }
    }

    public static final class Global {
        public static final Map<String, Integer> VALUES = new HashMap<>();

        public static synchronized int getInt(ContentResolver resolver, String key, int value) {
            return VALUES.getOrDefault(key, value);
        }
    }
}
