package android.content;

public interface SharedPreferences {
    int getInt(String name, int fallback);

    String getString(String name, String fallback);

    Editor edit();

    interface Editor {
        Editor putString(String name, String value);

        Editor putInt(String name, int value);

        Editor remove(String name);

        boolean commit();
    }
}
