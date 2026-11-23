package android.content;

import java.util.HashMap;
import java.util.Map;

/** In-memory preferences; commit publishes the edits at once. */
public class SharedPreferences {
    private final Map<String, Object> values = new HashMap<>();
    public boolean failCommits;

    public synchronized int getInt(String name, int fallback) {
        Object value = values.get(name);

        return value == null ? fallback : (Integer) value;
    }

    public synchronized String getString(String name, String fallback) {
        Object value = values.get(name);

        return value == null ? fallback : (String) value;
    }

    public Editor edit() {
        return new Editor();
    }

    /** Collects the writes of one commit. */
    public final class Editor {
        private final Map<String, Object> pending = new HashMap<>();

        public Editor putString(String name, String value) {
            pending.put(name, value);

            return this;
        }

        public Editor putInt(String name, int value) {
            pending.put(name, value);

            return this;
        }

        public boolean commit() {
            synchronized (SharedPreferences.this) {
                if (failCommits) {
                    return false;
                }

                values.putAll(pending);
            }

            return true;
        }
    }
}
