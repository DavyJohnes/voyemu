package android.os;

public class Bundle extends java.util.HashMap<String, Object> {
    public String getString(String key) {
        return (String) get(key);
    }

    public void putString(String key, String value) {
        put(key, value);
    }

    public void putInt(String key, int value) {
        put(key, value);
    }

    public void putLong(String key, long value) {
        put(key, value);
    }
}
