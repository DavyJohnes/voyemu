package android.content.pm;

public final class ResolveInfo {
    public static final class ActivityInfo {
        public String packageName, name;
    }

    public final ActivityInfo activityInfo = new ActivityInfo();

    public ResolveInfo(String pkg, String name) {
        activityInfo.packageName = pkg;
        activityInfo.name = name;
    }
}
