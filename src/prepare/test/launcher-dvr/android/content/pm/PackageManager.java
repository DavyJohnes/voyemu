package android.content.pm;

public class PackageManager {
    public boolean installed;

    public Object getPackageInfo(String name, int flags) throws Exception {
        if (!installed || !name.equals("com.qinggan.camera")) {
            throw new Exception();
        }

        return new Object();
    }
}
