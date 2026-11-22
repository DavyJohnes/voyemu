package org.json;

import java.util.ArrayList;
import java.util.List;

/** The org.json array subset of the component. */
public class JSONArray {
    final List<Object> values = new ArrayList<>();

    public JSONArray put(Object value) {
        values.add(value instanceof JSONObject ? ((JSONObject) value).values : value);

        return this;
    }

    public int length() {
        return values.size();
    }
}
