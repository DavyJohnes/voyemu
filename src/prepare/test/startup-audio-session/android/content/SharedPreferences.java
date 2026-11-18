package android.content;

public interface SharedPreferences {
    String getString(String name, String fallback);

    Editor edit();

    interface Editor {
        Editor putString(String name, String value);

        boolean commit();
    }
}
