package android.os;

public class Binder implements IBinder {
    public static int uid = 1000, pid = 70;

    public static int getCallingUid() {
        return uid;
    }

    public static int getCallingPid() {
        return pid;
    }

    @Override
    public boolean isBinderAlive() {
        return true;
    }
}
