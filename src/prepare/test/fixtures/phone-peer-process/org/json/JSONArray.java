package org.json;

import com.voyemu.prepare.PhonePeerJsonBridge;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;

public class JSONArray {
    final List<Object> values;

    public JSONArray() {
        values = new ArrayList<Object>();
    }

    public JSONArray(Collection source) {
        values = new ArrayList<Object>();

        for (Object value : source) {
            values.add(JSONObject.raw(value));
        }
    }

    public JSONArray(String text) {
        this((List) PhonePeerJsonBridge.parse(text));
    }

    public JSONArray put(Object value) {
        values.add(JSONObject.raw(value));

        return this;
    }

    public int length() {
        return values.size();
    }

    public Object get(int index) {
        return JSONObject.wrap(values.get(index));
    }

    public JSONObject getJSONObject(int index) {
        return (JSONObject) get(index);
    }

    public String getString(int index) {
        return (String) get(index);
    }

    @Override
    public String toString() {
        return PhonePeerJsonBridge.write(values);
    }
}
