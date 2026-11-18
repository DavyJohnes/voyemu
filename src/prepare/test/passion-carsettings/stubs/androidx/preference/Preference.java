package androidx.preference;

public class Preference {
    public interface OnPreferenceChangeListener {
        boolean onPreferenceChange(Preference p, Object value);
    }

    public String key, title;
    public OnPreferenceChangeListener listener;

    public void setPersistent(boolean value) {}

    public void setKey(String s) {
        key = s;
    }

    public void setTitle(CharSequence s) {
        title = s.toString();
    }

    public void setOnPreferenceChangeListener(OnPreferenceChangeListener l) {
        listener = l;
    }
}
