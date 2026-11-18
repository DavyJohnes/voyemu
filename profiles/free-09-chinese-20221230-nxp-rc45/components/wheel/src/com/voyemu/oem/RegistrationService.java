package com.voyemu.oem;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.util.Log;

import com.voyemu.input.WheelControl;

/** NXP key callbacks use the exact Launcher classes2 Binder contract. */
public final class RegistrationService extends Service {
    private static final KeyManagerStub KEY_MANAGER = new KeyManagerStub();

    public static KeyManagerStub keyManager() {
        return KEY_MANAGER;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        KEY_MANAGER.attach(this);

        try {
            Class.forName("android.os.ServiceManager")
                    .getMethod("addService", String.class, IBinder.class)
                    .invoke(null, "qg.key_manager", KEY_MANAGER);
            Log.i("VoyemuNxpWheel", "registered qg.key_manager");
        } catch (Exception error) {
            // checkstyle-allow IllegalCatch: Log hidden Binder registration failure; keep service.
            Log.e("VoyemuNxpWheel", "key manager registration failed", error);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        WheelControl.releaseExisting();
        super.onDestroy();
    }
}
