package android.content;

import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;

public class Context {
    public String packageName = "com.baidu.naviauto", opPackageName = "com.baidu.naviauto";
    public ApplicationInfo info = new ApplicationInfo();
    public PackageManager manager = new PackageManager();

    public String getPackageName() {
        return packageName;
    }

    public String getOpPackageName() {
        return opPackageName;
    }

    public ApplicationInfo getApplicationInfo() {
        return info;
    }

    public PackageManager getPackageManager() {
        return manager;
    }

    // Android11 ContextImpl(container, ...) retains container.mOpPackageName for
    // createPackageContext.
    public Context createPackageContext(String name, int flags) {
        Context c = new Context();

        c.packageName = name;
        c.opPackageName = opPackageName;

        return c;
    }
}
