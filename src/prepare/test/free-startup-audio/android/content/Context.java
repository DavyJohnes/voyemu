package android.content;

public class Context {
    public static final String AUDIO_SERVICE = "audio";
    public int permission = 0;
    public final android.media.AudioManager audio = new android.media.AudioManager();
    public final MemoryPreferences preferences = new MemoryPreferences();

    public SharedPreferences getSharedPreferences(String name, int mode) {
        return preferences;
    }

    public int checkCallingPermission(String permission) {
        return this.permission;
    }

    public Object getSystemService(String name) {
        return audio;
    }

    public static class MemoryPreferences implements SharedPreferences, SharedPreferences.Editor {
        public final java.util.Map<String, Object> values = new java.util.LinkedHashMap<>();

        public String getString(String key, String fallback) {
            return (String) values.getOrDefault(key, fallback);
        }

        public long getLong(String key, long fallback) {
            return (Long) values.getOrDefault(key, fallback);
        }

        public boolean getBoolean(String key, boolean fallback) {
            return (Boolean) values.getOrDefault(key, fallback);
        }

        public boolean contains(String key) {
            return values.containsKey(key);
        }

        public Editor edit() {
            return this;
        }

        public Editor clear() {
            values.clear();

            return this;
        }

        public Editor putString(String key, String value) {
            values.put(key, value);

            return this;
        }

        public Editor putLong(String key, long value) {
            values.put(key, value);

            return this;
        }

        public Editor putBoolean(String key, boolean value) {
            values.put(key, value);

            return this;
        }

        public boolean commit() {
            return true;
        }
    }
}
