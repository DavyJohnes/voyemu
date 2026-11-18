package android.os;

public class Binder {
    public static int uid = 2000;

    public static int getCallingUid() {
        return uid;
    }

    public static long clearCallingIdentity() {
        return 0;
    }

    public static void restoreCallingIdentity(long value) {}
}
