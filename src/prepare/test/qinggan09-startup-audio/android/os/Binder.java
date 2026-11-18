package android.os;

public class Binder {
    public static int uid = 2000, clears, restores;

    public static int getCallingUid() {
        return uid;
    }

    public static long clearCallingIdentity() {
        clears++;

        return 42;
    }

    public static void restoreCallingIdentity(long value) {
        if (value != 42) {
            throw new AssertionError("identity");
        }

        restores++;
    }
}
