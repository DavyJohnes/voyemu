package android.content;

import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;

public class Context {
    public String p;
    public ApplicationInfo info;
    public PackageManager pm;
    public Context own;
    public boolean fail;

    public String getPackageName() {
        return p;
    }

    public String getOpPackageName() {
        return p;
    }

    public ApplicationInfo getApplicationInfo() {
        return info;
    }

    public PackageManager getPackageManager() {
        return pm;
    }

    public Context createPackageContext(String p, int flags) throws Exception {
        if (fail) {
            throw new Exception();
        }

        return own;
    }
}
