package com.qinggan.canbus.service;

public final class RegistrationService extends android.app.Service {
    private ClimateService binder;

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
