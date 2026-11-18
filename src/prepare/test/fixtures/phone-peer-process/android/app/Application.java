package android.app;

public class Application {
    public static String getProcessName() {
        return android.os.PeerTestRuntime.CURRENT.get().name;
    }
}
