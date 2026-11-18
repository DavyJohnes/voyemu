package android.os;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

public class Bundle {
    private final Map<String, Object> values = new LinkedHashMap<String, Object>();

    public void putInt(String key, int value) {
        values.put(key, value);
    }

    public int getInt(String key) {
        return (Integer) values.get(key);
    }

    public void putString(String key, String value) {
        values.put(key, value);
    }

    public String getString(String key) {
        return (String) values.get(key);
    }

    public void putBinder(String key, IBinder value) {
        values.put(key, value);
    }

    public IBinder getBinder(String key) {
        return (IBinder) values.get(key);
    }

    public void putLong(String key, long value) {
        values.put(key, value);
    }

    public long getLong(String key) {
        return (Long) values.get(key);
    }

    public void putBoolean(String key, boolean value) {
        values.put(key, value);
    }

    public boolean getBoolean(String key) {
        return (Boolean) values.get(key);
    }

    public Object get(String key) {
        return values.get(key);
    }

    public Set<String> keySet() {
        return values.keySet();
    }
}
