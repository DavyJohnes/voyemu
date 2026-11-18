package android.content.pm;

public final class ProviderInfo {
    public String authority, packageName, name, readPermission, writePermission, processName;
    public boolean enabled, exported;
    public ApplicationInfo applicationInfo;
}
