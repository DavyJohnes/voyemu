package android.app;

import android.content.Context;
import android.content.Intent;
import android.os.IBinder;

public abstract class Service extends Context {
    public static final int START_NOT_STICKY = 2;

    public void onCreate() {}

    public abstract IBinder onBind(Intent intent);

    public boolean onUnbind(Intent intent) {
        return false;
    }

    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_NOT_STICKY;
    }
}
