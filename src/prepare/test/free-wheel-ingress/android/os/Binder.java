package android.os;

/** Own deterministic Binder boundary double; no Android/OEM implementation. */
public final class Binder {
    public static int uid = 1000, pid = 77, uidReads, pidReads;

    public static int getCallingUid() {
        uidReads++;

        return uid;
    }

    public static int getCallingPid() {
        pidReads++;

        return pid;
    }
}
