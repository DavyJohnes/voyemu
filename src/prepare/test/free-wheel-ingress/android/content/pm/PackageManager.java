package android.content.pm;

public abstract class PackageManager {
    public static final int SIGNATURE_MATCH = 0;

    public abstract ProviderInfo resolveContentProvider(String name, int flags);

    public abstract int checkSignatures(String first, String second);
}
