package org.json;

public class JSONArray {
    private final java.util.List<Integer> values = new java.util.ArrayList<>();

    public JSONArray put(int value) {
        values.add(value);

        return this;
    }

    public String toString() {
        return values.toString();
    }
}
