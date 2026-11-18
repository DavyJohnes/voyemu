package org.json;

import com.voyemu.prepare.PhonePeerJsonBridge;

import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class JSONObject {
    public static final Object NULL = new Object();
    final Map<String, Object> values;

    public JSONObject() {
        values = new LinkedHashMap<String, Object>();
    }

    public JSONObject(String text) throws JSONException {
        this((Map) PhonePeerJsonBridge.parse(text));
    }

    public JSONObject(Map map) {
        values = new LinkedHashMap<String, Object>();

        for (Object key : map.keySet()) {
            values.put((String) key, map.get(key));
        }
    }

    static Object raw(Object value) {
        if (value == NULL) {
            return null;
        }

        if (value instanceof JSONObject) {
            return ((JSONObject) value).values;
        }

        if (value instanceof JSONArray) {
            return ((JSONArray) value).values;
        }

        return value;
    }

    static Object wrap(Object value) {
        if (value == null) {
            return NULL;
        }

        if (value instanceof Map) {
            return new JSONObject((Map) value);
        }

        if (value instanceof List) {
            return new JSONArray((List) value);
        }

        return value;
    }

    public JSONObject put(String key, Object value) {
        values.put(key, raw(value));

        return this;
    }

    public JSONObject put(String key, boolean value) {
        return put(key, Boolean.valueOf(value));
    }

    public JSONObject put(String key, int value) {
        return put(key, Integer.valueOf(value));
    }

    public JSONObject put(String key, long value) {
        return put(key, Long.valueOf(value));
    }

    public JSONObject put(String key, double value) {
        return put(key, Double.valueOf(value));
    }

    public Object get(String key) {
        if (!values.containsKey(key)) {
            throw new IllegalArgumentException("missing " + key);
        }

        return wrap(values.get(key));
    }

    public Object remove(String key) {
        return wrap(values.remove(key));
    }

    public Object opt(String key) {
        return wrap(values.get(key));
    }

    public boolean has(String key) {
        return values.containsKey(key);
    }

    public int length() {
        return values.size();
    }

    public Iterator<String> keys() {
        return values.keySet().iterator();
    }

    public JSONObject getJSONObject(String key) {
        return (JSONObject) get(key);
    }

    public JSONArray getJSONArray(String key) {
        return (JSONArray) get(key);
    }

    public String getString(String key) {
        return (String) get(key);
    }

    public boolean getBoolean(String key) {
        return (Boolean) get(key);
    }

    public int getInt(String key) {
        return ((Number) get(key)).intValue();
    }

    public long getLong(String key) {
        return ((Number) get(key)).longValue();
    }

    public String optString(String key) {
        Object value = opt(key);

        return value instanceof String ? (String) value : "";
    }

    @Override
    public String toString() {
        return PhonePeerJsonBridge.write(values);
    }
}
