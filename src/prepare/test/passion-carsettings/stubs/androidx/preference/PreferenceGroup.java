package androidx.preference;

import android.content.Context;

import java.util.LinkedHashMap;
import java.util.Map;

public class PreferenceGroup extends Preference {
    public Context context;
    public Map<String, Preference> items = new LinkedHashMap<>();

    public PreferenceGroup(Context c) {
        context = c;
    }

    public Context getContext() {
        return context;
    }

    public Preference findPreference(CharSequence key) {
        return items.get(key.toString());
    }

    public boolean addPreference(Preference p) {
        items.put(p.key, p);

        return true;
    }
}
