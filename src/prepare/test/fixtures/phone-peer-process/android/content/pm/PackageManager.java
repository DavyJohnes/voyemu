package android.content.pm;

public class PackageManager {
    public static final int SIGNATURE_MATCH = 0;

    public ApplicationInfo getApplicationInfo(String name, int flags) {
        return new ApplicationInfo();
    }

    public int checkSignatures(String first, String second) {
        return SIGNATURE_MATCH;
    }

    public ProviderInfo resolveContentProvider(String authority, int flags) {
        if (!"com.voyemu.input.phone.peer".equals(authority)) {
            return null;
        }

        ProviderInfo p = new ProviderInfo();
        p.packageName = "com.qinggan.bluetoothphone";
        p.applicationInfo = new ApplicationInfo();

        return p;
    }
}
