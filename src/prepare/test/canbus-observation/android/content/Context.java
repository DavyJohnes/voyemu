package android.content;

public class Context {
    public <T> T getSystemService(Class<T> type) {
        throw new AssertionError();
    }

    public Intent registerReceiver(BroadcastReceiver r, IntentFilter f) {
        throw new AssertionError();
    }

    public void unregisterReceiver(BroadcastReceiver r) {
        throw new AssertionError();
    }
}
