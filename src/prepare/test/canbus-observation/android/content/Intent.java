package android.content;

import java.util.HashMap;
import java.util.Map;

public class Intent {
    public static final String ACTION_SCREEN_OFF = "android.intent.action.SCREEN_OFF";
    public static final String ACTION_SCREEN_ON = "android.intent.action.SCREEN_ON";
    private final String action;
    private final Map<String, Object> extras = new HashMap<>();

    public Intent(String action) {
        this.action = action;
    }

    public String getAction() {
        return action;
    }

    public Intent putExtra(String name, String value) {
        extras.put(name, value);

        return this;
    }

    public Intent putExtra(String name, int value) {
        extras.put(name, value);

        return this;
    }

    public boolean hasExtra(String name) {
        return extras.containsKey(name);
    }

    public String getStringExtra(String name) {
        return (String) extras.get(name);
    }

    public int getIntExtra(String name, int fallback) {
        return (int) extras.getOrDefault(name, fallback);
    }

    public long getLongExtra(String name, long fallback) {
        return (long) extras.getOrDefault(name, fallback);
    }
}
