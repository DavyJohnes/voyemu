package android.app;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.os.IBinder;

public abstract class Service extends Context {
    public static final int START_NOT_STICKY = 2;

    public abstract IBinder onBind(Intent intent);

    public int onStartCommand(Intent intent, int flags, int startId) {
        return 0;
    }

    public void onDestroy() {}

    public Object getSystemService(String name) {
        return null;
    }

    public SharedPreferences getSharedPreferences(String name, int mode) {
        return null;
    }

    public ComponentName startService(Intent intent) {
        return null;
    }

    public void startActivity(Intent intent, Bundle options) {}

    public void stopSelf() {}
}
