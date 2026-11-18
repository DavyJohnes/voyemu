package android.content;

import android.content.pm.PackageManager;

public class Context {
    public static final String CAMERA_SERVICE = "camera";
    public Object service;
    public PackageManager packages = new PackageManager();

    public Object getSystemService(String name) {
        return service;
    }

    public PackageManager getPackageManager() {
        return packages;
    }
}
