package android.os;

public class Environment {
    public static final String MEDIA_MOUNTED = "mounted";
    public static java.io.File directory = new java.io.File("/storage/emulated/0");
    public static String state = "unmounted";

    public static String getExternalStorageState() {
        return state;
    }

    public static java.io.File getExternalStorageDirectory() {
        return directory;
    }
}
