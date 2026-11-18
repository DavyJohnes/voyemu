package android.os;

public class Binder {
    public static int uid = 0;

    public static int getCallingUid() {
        return uid;
    }

    public static int getCallingPid() {
        return 123;
    }

    public static long clearCallingIdentity() {
        return 0;
    }

    public static void restoreCallingIdentity(long identity) {}
}
