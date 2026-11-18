package androidx.preference;

import android.content.Context;

public class Preference {
    public interface OnPreferenceChangeListener {
        boolean onPreferenceChange(Preference p, Object value);
    }

    public Context getContext() {
        return null;
    }

    public void setPersistent(boolean value) {}

    public void setKey(String s) {}

    public void setTitle(CharSequence s) {}

    public void setOnPreferenceChangeListener(OnPreferenceChangeListener l) {}
}
