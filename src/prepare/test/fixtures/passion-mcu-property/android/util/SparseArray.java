package android.util;

public class SparseArray<T> {
    private final java.util.Map<Integer, T> m = new java.util.HashMap<>();

    public T get(int key) {
        return m.get(key);
    }

    public void put(int key, T value) {
        m.put(key, value);
    }
}
