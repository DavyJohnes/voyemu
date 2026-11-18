package android.content.pm;

public class PackageManager {
    public static final int SIGNATURE_MATCH = 0;
    public int uid = 10130, signature = 0;
    public String[] packages = {"com.baidu.naviauto"};
    public boolean missing;

    public static class NameNotFoundException extends Exception {}

    public static class ApplicationInfo {
        public int uid;
    }

    public ApplicationInfo getApplicationInfo(String p, int f) throws NameNotFoundException {
        if (missing) {
            throw new NameNotFoundException();
        }

        ApplicationInfo a = new ApplicationInfo();
        a.uid = uid;

        return a;
    }

    public int checkSignatures(String a, String b) {
        return signature;
    }

    public String[] getPackagesForUid(int u) {
        return packages;
    }
}
