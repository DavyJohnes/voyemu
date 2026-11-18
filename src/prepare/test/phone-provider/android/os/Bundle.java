package android.os;

public class Bundle extends java.util.HashMap<String, Object> {
    public String getString(String k) {
        return (String) get(k);
    }

    public int getInt(String k) {
        return (Integer) get(k);
    }

    public int getInt(String k, int d) {
        return containsKey(k) ? getInt(k) : d;
    }

    public long getLong(String k, long d) {
        return containsKey(k) ? (Long) get(k) : d;
    }

    public void putString(String k, String v) {
        put(k, v);
    }

    public void putInt(String k, int v) {
        put(k, v);
    }

    public void putLong(String k, long v) {
        put(k, v);
    }
}
