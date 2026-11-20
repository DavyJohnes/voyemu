package com.voyemu.oem;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.util.Log;

import java.lang.reflect.Method;

/** Registers the OEM screen-power Binder and the shared OEM car signal Binder for this profile. */
public final class RegistrationService extends Service {
    private PowerManagerStub power;

    @Override
    public void onCreate() {
        super.onCreate();
        NotificationManager manager = getSystemService(NotificationManager.class);
        manager.createNotificationChannel(
                new NotificationChannel(
                        "oem_power", "OEM screen power", NotificationManager.IMPORTANCE_MIN));
        startForeground(
                1002,
                new Notification.Builder(this, "oem_power")
                        .setSmallIcon(android.R.drawable.stat_notify_sync_noanim)
                        .setContentTitle("OEM screen power")
                        .setOngoing(true)
                        .build());
        power = new PowerManagerStub(this);

        try {
            Method add =
                    Class.forName("android.os.ServiceManager")
                            .getMethod("addService", String.class, IBinder.class);
            add.invoke(null, "qg.power_manager", power);
            Log.i("VoyemuOemPower", "registered qg.power_manager");
            add.invoke(null, "qg.carsignal", new CarSignalStub());
            Log.i("VoyemuOemPower", "registered qg.carsignal");
        } catch (Exception failure) {
            // checkstyle-allow IllegalCatch: reflection or platform registration failure becomes an
            // OEM power startup failure
            throw new IllegalStateException("Cannot register OEM power services", failure);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return power;
    }
}
