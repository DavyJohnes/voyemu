package com.voyemu.oem;

import android.content.ComponentName;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;

import java.util.List;

/** Preserve the OEM choice while supplying Android's complete HOME candidate set. */
public final class HomePreference {
    private HomePreference() {}

    public static void add(
            PackageManager pm,
            IntentFilter filter,
            int match,
            ComponentName[] ignored,
            ComponentName selected) {
        Intent intent = new Intent(Intent.ACTION_MAIN);

        intent.addCategory(Intent.CATEGORY_HOME);
        intent.addCategory(Intent.CATEGORY_DEFAULT);
        List<ResolveInfo> matches = pm.queryIntentActivities(intent, PackageManager.GET_META_DATA);
        ComponentName[] candidates = new ComponentName[matches.size()];
        boolean found = false;

        for (int i = 0; i < matches.size(); i++) {
            ResolveInfo info = matches.get(i);
            candidates[i] =
                    new ComponentName(info.activityInfo.packageName, info.activityInfo.name);
            found |= candidates[i].equals(selected);
        }

        if (!found) {
            throw new IllegalArgumentException("Configured HOME is not a current candidate");
        }

        pm.addPreferredActivity(filter, match, candidates, selected);
    }
}
