package com.qinggan.canbus.service;

public final class RegistrationService extends android.app.Service {
    private static volatile ClimateService sBinder;
    private ClimateService binder;

    static org.json.JSONObject modeHistory(String request) throws org.json.JSONException {
        ClimateService current = sBinder;

        return current == null
                ? new org.json.JSONObject()
                        .put("schema", 1)
                        .put("source", "android:qg.canbus:mode")
                        .put("status", "not-ready")
                : current.modeHistory(request);
    }

    @Override
    public void onCreate() {
        super.onCreate();
        android.app.NotificationManager manager =
                getSystemService(android.app.NotificationManager.class);
        manager.createNotificationChannel(
                new android.app.NotificationChannel(
                        "nxp_canbus", "NXP CAN", android.app.NotificationManager.IMPORTANCE_MIN));
        startForeground(
                1001,
                new android.app.Notification.Builder(this, "nxp_canbus")
                        .setSmallIcon(android.R.drawable.stat_notify_sync_noanim)
                        .setContentTitle("NXP climate")
                        .build());
        binder = new ClimateService(this);

        try {
            Class.forName("android.os.ServiceManager")
                    .getMethod("addService", String.class, android.os.IBinder.class)
                    .invoke(null, "qg.canbus", binder);
        } catch (ReflectiveOperationException e) {
            throw new IllegalStateException("qg.canbus registration", e);
        }

        sBinder = binder;
        // The NXP fixture starts with a fixed ACC ON state.
        binder.powerMode(2);
    }

    @Override
    public void onDestroy() {
        if (sBinder == binder) {
            sBinder = null;
        }

        super.onDestroy();
    }

    @Override
    public int onStartCommand(android.content.Intent intent, int flags, int id) {
        return START_STICKY;
    }

    @Override
    public android.os.IBinder onBind(android.content.Intent intent) {
        return binder;
    }
}
