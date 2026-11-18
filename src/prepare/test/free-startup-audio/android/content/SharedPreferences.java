package android.content;

public interface SharedPreferences {
    String getString(String key, String fallback);

    long getLong(String key, long fallback);

    boolean getBoolean(String key, boolean fallback);

    boolean contains(String key);

    Editor edit();

    interface Editor {
        Editor clear();

        Editor putString(String key, String value);

        Editor putLong(String key, long value);

        Editor putBoolean(String key, boolean value);

        boolean commit();
    }
}
