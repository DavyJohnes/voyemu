package com.voyemu.prepare;

import android.content.ComponentName;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;

import com.voyemu.oem.HomePreference;

import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;

public final class HomePreferenceTest {
    static final ComponentName MAIN =
            new ComponentName(
                    "com.qinggan.app.launcher", "com.qinggan.app.launcher.activity.MainActivity");
    static final ComponentName LOGIN =
            new ComponentName("com.qinggan.app.login", "com.qinggan.app.login.RootActivity");
    static final ComponentName SECOND =
            new ComponentName(
                    "com.qinggan.app.launcher",
                    "com.qinggan.secondlauncher.activity.SecondMainActivity");
    static final ComponentName FALLBACK =
            new ComponentName("com.android.settings", "com.android.settings.FallbackHome");

    static final class PM extends PackageManager {
        List<ResolveInfo> all =
                Arrays.asList(info(MAIN), info(LOGIN), info(SECOND), info(FALLBACK));
        ComponentName[] written;
        ComponentName target;
        IntentFilter filter;
        int match;

        @Override
        public List<ResolveInfo> queryIntentActivities(Intent intent, int flags) {
            check(
                    intent.action.equals(Intent.ACTION_MAIN)
                            && intent.categories.equals(
                                    new HashSet<>(
                                            Arrays.asList(
                                                    Intent.CATEGORY_HOME, Intent.CATEGORY_DEFAULT)))
                            && flags == GET_META_DATA);

            return all;
        }

        @Override
        public void addPreferredActivity(
                IntentFilter f, int m, ComponentName[] set, ComponentName chosen) {
            filter = f;
            match = m;
            written = set;
            target = chosen;
        }
    }

    static ResolveInfo info(ComponentName c) {
        return new ResolveInfo(c.pkg, c.name);
    }

    static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    static boolean sameSet(ComponentName[] a, ComponentName[] b) {
        return a.length == b.length && Arrays.asList(a).containsAll(Arrays.asList(b));
    }

    public static void main(String[] args) throws Exception {
        check(args.length == 0);
        PM pm = new PM();
        IntentFilter f = new IntentFilter();
        ComponentName[] old = {LOGIN, MAIN};
        ComponentName[] full = {MAIN, LOGIN, SECOND, FALLBACK};
        check(!sameSet(old, full));
        HomePreference.add(pm, f, 0x100000, old, MAIN);
        check(
                pm.filter == f
                        && pm.match == 0x100000
                        && pm.target.equals(MAIN)
                        && sameSet(pm.written, full));
        check(
                !sameSet(
                        pm.written,
                        new ComponentName[] {
                            MAIN, LOGIN, SECOND, FALLBACK, new ComponentName("new", "new.Home")
                        }));
        pm.all = Collections.singletonList(info(LOGIN));
        pm.written = null;
        boolean failed = false;

        try {
            HomePreference.add(pm, f, 0x100000, old, MAIN);
        } catch (IllegalArgumentException e) {
            failed = true;
        }

        check(failed && pm.written == null);
    }
}
