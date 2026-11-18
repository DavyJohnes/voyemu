package android.app;

public class Service extends android.content.Context {
    public static final int START_STICKY = 1;

    public void onCreate() {}

    public void onDestroy() {}

    public void startForeground(int id, Notification n) {
        throw new AssertionError();
    }

    public int onStartCommand(android.content.Intent i, int f, int s) {
        return 0;
    }

    public android.os.IBinder onBind(android.content.Intent i) {
        return null;
    }
}
