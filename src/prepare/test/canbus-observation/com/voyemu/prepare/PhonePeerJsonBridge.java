package com.voyemu.prepare;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Integer/Long boundary for the existing org.json doubles; fractions retain BigDecimal type. */
public final class PhonePeerJsonBridge {
    public static Object parse(String text) {
        return integers(Json.parse(text));
    }

    private static Object integers(Object value) {
        if (value instanceof Long number
                && number >= Integer.MIN_VALUE
                && number <= Integer.MAX_VALUE) {
            return Integer.valueOf(number.intValue());
        }

        if (value instanceof Map<?, ?> map) {
            var converted = new LinkedHashMap<String, Object>();

            for (var entry : map.entrySet()) {
                converted.put((String) entry.getKey(), integers(entry.getValue()));
            }

            return converted;
        } else if (value instanceof List<?> list) {
            var converted = new ArrayList<Object>();

            for (Object item : list) {
                converted.add(integers(item));
            }

            return converted;
        }

        return value;
    }

    public static String write(Object value) {
        return Json.write(value);
    }
}
