package com.voyemu.oem;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.os.Process;
import android.util.Log;

import com.voyemu.input.WheelControl;

import java.lang.reflect.Method;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Registers the four OEM binder stubs (qg.power_manager, qg.carsignal, qg.key_manager,
 * dls_tbox_service) in android.os.ServiceManager and keeps the process alive. Same pattern as
 * com.qinggan.canbus.service RegistrationService: hidden addService API via reflection works
 * because this app is platform-signed and shares android.uid.system.
 */
public class RegistrationService extends Service {
    private static final String TAG = "VoyemuOemStub";
    private static final String CHANNEL = "oem_services_stub";
    private static final KeyManagerStub KEY_MANAGER = new KeyManagerStub();
    private static final Map<String, IBinder> STUBS = new LinkedHashMap<>();

    static {
        STUBS.put("qg.carsignal", new CarSignalStub());
        STUBS.put("qg.key_manager", KEY_MANAGER);
        STUBS.put("dls_tbox_service", new TBoxStub());
    }

    @Override
    public void onCreate() {
        super.onCreate();
        KEY_MANAGER.attach(this);

        if (!STUBS.containsKey("qg.power_manager")) {
            STUBS.put("qg.power_manager", new PowerManagerStub(this));
        }

        NotificationManager nm = getSystemService(NotificationManager.class);
        NotificationChannel ch =
                new NotificationChannel(
                        CHANNEL, "OEM services stub", NotificationManager.IMPORTANCE_MIN);
        nm.createNotificationChannel(ch);
        Notification n =
                new Notification.Builder(this, CHANNEL)
                        .setContentTitle("qg power/carsignal/key/tbox emulator stubs")
                        .setSmallIcon(android.R.drawable.stat_notify_sync_noanim)
                        .setOngoing(true)
                        .build();
        startForeground(1002, n);

        try {
            Method add =
                    Class.forName("android.os.ServiceManager")
                            .getMethod("addService", String.class, IBinder.class);

            for (Map.Entry<String, IBinder> e : STUBS.entrySet()) {
                try {
                    add.invoke(null, e.getKey(), e.getValue());
                    Log.i(TAG, "registered " + e.getKey() + " (uid " + Process.myUid() + ")");
                } catch (ReflectiveOperationException ex) {
                    Log.e(TAG, "failed to register " + e.getKey(), ex);
                }
            }
        } catch (ReflectiveOperationException e) {
            Log.e(TAG, "ServiceManager reflection unavailable", e);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        WheelControl.releaseExisting();
        super.onDestroy();
    }

    public static KeyManagerStub keyManager() {
        return KEY_MANAGER;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
