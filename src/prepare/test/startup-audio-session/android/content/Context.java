package android.content;

public abstract class Context {
    public static final String AUDIO_SERVICE = "audio";
    public static final int MODE_PRIVATE = 0;

    public abstract Object getSystemService(String name);

    public abstract SharedPreferences getSharedPreferences(String name, int mode);
}
