package org.json;

public class JSONObject {
    public static final Object NULL = new Object();
    private final java.util.Map<String, Object> values = new java.util.LinkedHashMap<>();

    public JSONObject() {}

    public JSONObject(java.util.Map<?, ?> source) {
        for (var e : source.entrySet()) {
            values.put((String) e.getKey(), e.getValue());
        }
    }

    public JSONObject(String text) {
        if (!text.startsWith("{") || !text.endsWith("}")) {
            throw new IllegalArgumentException("json");
        }

        java.util.regex.Matcher m =
                java.util.regex.Pattern.compile(
                                "\"([^\"]+)\":(\"[^\"]*\"|true|false|null|-?[0-9]+)")
                        .matcher(text);

        while (m.find()) {
            String v = m.group(2);
            values.put(
                    m.group(1),
                    v.startsWith("\"")
                            ? v.substring(1, v.length() - 1)
                            : v.equals("true")
                                    ? Boolean.TRUE
                                    : v.equals("false")
                                            ? Boolean.FALSE
                                            : v.equals("null") ? NULL : Long.valueOf(v));
        }
    }

    public JSONObject put(String key, Object value) {
        values.put(key, value);

        return this;
    }

    public String getString(String key) {
        Object value = values.get(key);

        if (!(value instanceof String)) {
            throw new IllegalArgumentException(key);
        }

        return (String) value;
    }

    public Object get(String key) {
        return values.get(key);
    }

    public java.util.Iterator<String> keys() {
        return values.keySet().iterator();
    }

    public String toString() {
        StringBuilder out = new StringBuilder("{");

        for (var e : values.entrySet()) {
            if (out.length() > 1) {
                out.append(',');
            }

            out.append('"').append(e.getKey()).append("\":");
            Object v = e.getValue();

            if (v instanceof String) {
                out.append('"').append(v).append('"');
            } else {
                out.append(v == NULL ? "null" : v);
            }
        }

        return out.append('}').toString();
    }
}
