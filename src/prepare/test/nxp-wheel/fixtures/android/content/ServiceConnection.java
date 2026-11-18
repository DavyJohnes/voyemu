package android.content;

public interface ServiceConnection {
    void onServiceConnected(ComponentName name, android.os.IBinder binder);

    void onServiceDisconnected(ComponentName name);
}
