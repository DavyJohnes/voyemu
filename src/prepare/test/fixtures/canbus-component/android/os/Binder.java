package android.os;

public class Binder {
    public static int getCallingUid() {
        return 1000;
    }

    public static int getCallingPid() {
        return 4242;
    }
}
