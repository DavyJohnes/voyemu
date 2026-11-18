package android.content.pm;

public final class PackageManager {
    public String[] packages = {"com.baidu.naviauto"};

    public String[] getPackagesForUid(int uid) {
        return packages;
    }
}
