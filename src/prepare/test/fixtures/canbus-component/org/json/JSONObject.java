package org.json;

import com.voyemu.prepare.PhonePeerJsonBridge;

import java.util.LinkedHashMap;
import java.util.Map;

/** The org.json subset of the component with the checked exceptions of the platform API. */
public class JSONObject {
    private final Map<String, Object> values;

    public JSONObject() {
        values = new LinkedHashMap<>();
    }

    @SuppressWarnings("unchecked")
    public JSONObject(String text) throws JSONException {
        values = (Map<String, Object>) PhonePeerJsonBridge.parse(text);
    }

    public JSONObject put(String key, Object value) throws JSONException {
        values.put(key, value instanceof JSONObject ? ((JSONObject) value).values : value);

        return this;
    }

    public JSONObject put(String key, int value) throws JSONException {
        return put(key, (Object) value);
    }

    public JSONObject put(String key, long value) throws JSONException {
        return put(key, (Object) value);
    }

    public Object get(String key) throws JSONException {
        if (!values.containsKey(key)) {
            throw new JSONException("missing " + key);
        }

        Object value = values.get(key);

        return value instanceof Map ? new JSONObject(PhonePeerJsonBridge.write(value)) : value;
    }

    public JSONObject getJSONObject(String key) throws JSONException {
        return (JSONObject) get(key);
    }

    public int length() {
        return values.size();
    }

    @Override
    public String toString() {
        return PhonePeerJsonBridge.write(values);
    }
}
