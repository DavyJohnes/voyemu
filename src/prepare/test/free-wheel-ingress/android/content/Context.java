package android.content;

import android.content.pm.PackageManager;

public abstract class Context {
    public static final String ACTIVITY_SERVICE = "activity";

    public abstract String getPackageName();

    public abstract PackageManager getPackageManager();

    public abstract Object getSystemService(String name);
}
