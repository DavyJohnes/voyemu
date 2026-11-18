package android.os;

import java.util.HashMap;
import java.util.Map;

public class Bundle {
    public final Map<String, String> values = new HashMap<>();

    public boolean isEmpty() {
        return values.isEmpty();
    }

    public void putString(String k, String v) {
        values.put(k, v);
    }

    public String getString(String k) {
        return values.get(k);
    }
}
