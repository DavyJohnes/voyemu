package android.os;

public class Binder {
    public static int uid = 10130, clears, restores;

    public static int getCallingUid() {
        return uid;
    }

    public static long clearCallingIdentity() {
        clears++;

        return 42;
    }

    public static void restoreCallingIdentity(long x) {
        if (x != 42) {
            throw new AssertionError();
        }

        restores++;
    }
}
