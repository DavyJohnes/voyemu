package android.os;

public class Binder {
    public static int uid = 10130, clears, restores;
    public static boolean cleared;

    public static int getCallingUid() {
        return uid;
    }

    public static long clearCallingIdentity() {
        if (cleared) {
            throw new AssertionError("nested identity");
        }

        cleared = true;
        clears++;

        return 42;
    }

    public static void restoreCallingIdentity(long x) {
        if (x != 42 || !cleared) {
            throw new AssertionError();
        }

        cleared = false;
        restores++;
    }
}
