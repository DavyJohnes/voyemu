package android.os;

// JVM double of android.os.Process.
@SuppressWarnings("AvoidCommonTypeNames")
public class Process {
    public static int myPid() {
        return 42;
    }

    public static int myUid() {
        return 1000;
    }
}
