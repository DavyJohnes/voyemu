package android.content;

import android.media.AudioManager;

public class Context {
    public static final String AUDIO_SERVICE = "audio";
    public AudioManager manager;

    public Context(AudioManager m) {
        manager = m;
    }

    public Object getSystemService(String name) {
        return manager;
    }
}
