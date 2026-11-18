package android.os;

import java.util.HashMap;
import java.util.Map;

public class Bundle {
    public final Map<String, Object> values = new HashMap<>();

    public boolean isEmpty() {
        return values.isEmpty();
    }

    public void putString(String k, String v) {
        values.put(k, v);
    }

    public String getString(String k) {
        return (String) values.get(k);
    }

    public void putInt(String k, int v) {
        values.put(k, v);
    }

    public int getInt(String k, int fallback) {
        Object v = values.get(k);

        return v instanceof Integer ? (Integer) v : fallback;
    }

    public int getInt(String k) {
        Object v = values.get(k);

        return v instanceof Integer ? (Integer) v : 0;
    }
}
