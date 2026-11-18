package android.content;

public class Context {
    public static final String AUDIO_SERVICE = "audio";
    public Object service;

    public Object getSystemService(String name) {
        return service;
    }
}
