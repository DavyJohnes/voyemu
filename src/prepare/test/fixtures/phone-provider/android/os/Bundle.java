package android.os;

public class Bundle extends java.util.HashMap<String, Object> {
    public void putBoolean(String k, boolean v) {
        put(k, v);
    }

    public void putLong(String k, long v) {
        put(k, v);
    }

    public long getLong(String k, long d) {
        return containsKey(k) ? (Long) get(k) : d;
    }

    public void putInt(String k, int v) {
        put(k, v);
    }

    public void putString(String k, String v) {
        put(k, v);
    }

    public void putBinder(String k, IBinder v) {
        put(k, v);
    }

    public IBinder getBinder(String k) {
        return (IBinder) get(k);
    }

    public String getString(String k) {
        return (String) get(k);
    }
}
