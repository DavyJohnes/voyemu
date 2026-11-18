package android.os;

// JVM double of android.os.Process.
@SuppressWarnings("AvoidCommonTypeNames")
public final class Process {
    public static int myUid() {
        return PeerTestRuntime.CURRENT.get().uid;
    }

    public static int myPid() {
        return PeerTestRuntime.CURRENT.get().pid;
    }
}
