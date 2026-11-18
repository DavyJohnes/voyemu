package androidx.preference;

public class PreferenceGroup extends Preference {
    // Mirrors the androidx signature.
    @SuppressWarnings("TypeParameterUnusedInFormals")
    public <T extends Preference> T findPreference(CharSequence key) {
        return null;
    }

    public boolean addPreference(Preference p) {
        return false;
    }
}
