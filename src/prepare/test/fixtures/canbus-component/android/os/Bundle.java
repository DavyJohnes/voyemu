package android.os;

import java.util.HashMap;
import java.util.Map;

public class Bundle {
    private final Map<String, Integer> values = new HashMap<>();

    public boolean containsKey(String key) {
        return values.containsKey(key);
    }

    public int getInt(String key) {
        Integer value = values.get(key);

        return value == null ? 0 : value;
    }

    public void putInt(String key, int value) {
        values.put(key, value);
    }

    public java.util.Set<String> keySet() {
        return values.keySet();
    }

    public Object get(String key) {
        return values.get(key);
    }
}
