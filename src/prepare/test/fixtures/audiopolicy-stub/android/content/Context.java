package android.content;

import android.media.AudioManager;

import java.util.HashMap;
import java.util.Map;

public class Context {
    public static final String AUDIO_SERVICE = "audio";
    public static final int MODE_PRIVATE = 0;
    public static final AudioManager AUDIO = new AudioManager();
    private static final Map<String, SharedPreferences> PREFERENCES = new HashMap<>();

    public SharedPreferences getSharedPreferences(String name, int mode) {
        return PREFERENCES.computeIfAbsent(name, key -> new SharedPreferences());
    }

    public Object getSystemService(String name) {
        return AUDIO_SERVICE.equals(name) ? AUDIO : null;
    }
}
