package android.content;

public abstract class Context {
    public static final String AUDIO_SERVICE = "audio";
    public static final int MODE_PRIVATE = 0;
    public int permission;

    public int checkCallingPermission(String name) {
        return permission;
    }

    public abstract Object getSystemService(String name);

    public abstract SharedPreferences getSharedPreferences(String name, int mode);
}
