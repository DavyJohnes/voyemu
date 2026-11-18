package android.content.pm;

import android.content.ComponentName;
import android.content.Intent;
import android.content.IntentFilter;

import java.util.List;

public abstract class PackageManager {
    public static final int GET_META_DATA = 128;

    public abstract List<ResolveInfo> queryIntentActivities(Intent intent, int flags);

    public abstract void addPreferredActivity(
            IntentFilter filter, int match, ComponentName[] set, ComponentName selected);
}
