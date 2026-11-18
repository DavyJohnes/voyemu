package android.content.pm;

public class PackageManager {
    public static final int SIGNATURE_MATCH = 0;
    public static final int PERMISSION_GRANTED = 0;
    public static int signature;
    public static int packageUid = 1000;

    public static class NameNotFoundException extends Exception {}

    public int getPackageUid(String name, int flags) throws NameNotFoundException {
        return packageUid;
    }

    public int checkSignatures(String a, String b) {
        return signature;
    }
}
