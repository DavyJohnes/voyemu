package android.content;

import java.util.HashMap;
import java.util.Map;

public class SharedPreferences {
    private final Map<String, Integer> values = new HashMap<>();

    public int getInt(String name, int fallback) {
        return values.getOrDefault(name, fallback);
    }

    public Editor edit() {
        return new Editor();
    }

    public final class Editor {
        private final Map<String, Integer> pending = new HashMap<>();

        public Editor putInt(String name, int value) {
            pending.put(name, value);

            return this;
        }

        public boolean commit() {
            values.putAll(pending);

            return true;
        }
    }
}
