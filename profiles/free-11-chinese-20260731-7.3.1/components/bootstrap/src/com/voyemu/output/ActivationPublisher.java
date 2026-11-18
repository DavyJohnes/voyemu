package com.voyemu.output;

import android.os.AsyncTask;
import android.os.IBinder;
import android.os.Parcel;
import android.util.Log;

import java.lang.reflect.Method;

/**
 * Publishes the emulator's own NVRAM activation fixture through the original property notification
 * contract.
 */
final class ActivationPublisher {
    private static final String TAG = "VoyemuBootstrap";
    private static final int SYSTEM_PROPERTIES_TRANSACTION = 1599295570;
    private static boolean inFlight;
    private static boolean attempted;

    static synchronized void publish() {
        if (inFlight || attempted) {
            return;
        }

        inFlight = true;

        try {
            AsyncTask.execute(
                    new Runnable() {
                        @Override
                        public void run() {
                            publishOnce();
                        }
                    });
        } catch (RuntimeException e) {
            // checkstyle-allow IllegalCatch: any executor rejection clears in-flight state so
            // scheduling can retry
            inFlight = false;
            Log.w(TAG, "Activation fixture publication was not scheduled", e);
        }
    }

    private static void publishOnce() {
        Parcel data = null;

        try {
            Object value =
                    Class.forName("com.qinggan.os.SystemProperties")
                            .getMethod("nvramRead", int.class)
                            .invoke(null, 8);

            if (!"true".equals(value)) {
                Log.i(TAG, "Own NVRAM does not confirm an active emulator fixture");

                return;
            }

            Class<?> properties = Class.forName("android.os.SystemProperties");
            Method get = properties.getMethod("get", String.class, String.class);
            Method set = properties.getMethod("set", String.class, String.class);
            Object baseline = get.invoke(null, "ignoreActivation", "");

            if (!(baseline instanceof String)) {
                throw new IllegalStateException("Property read unavailable");
            }

            IBinder activity =
                    (IBinder)
                            Class.forName("android.os.ServiceManager")
                                    .getDeclaredMethod("checkService", String.class)
                                    .invoke(null, "activity");

            if (activity == null) {
                throw new IllegalStateException(
                        "Activity property notification service unavailable");
            }

            data = Parcel.obtain();

            synchronized (ActivationPublisher.class) {
                attempted = true;
            }

            set.invoke(null, "ignoreActivation", "true");

            if (!"true".equals(get.invoke(null, "ignoreActivation", ""))) {
                throw new IllegalStateException("Activation property readback differs");
            }

            if (!activity.transact(SYSTEM_PROPERTIES_TRANSACTION, data, null, 0)) {
                throw new IllegalStateException("Activity property notification was not accepted");
            }

            Log.i(TAG, "Own active fixture property published; activity notification issued");
        } catch (Exception | LinkageError e) {
            // checkstyle-allow IllegalCatch: reflection or Binder failure reports publication
            // uncertainty without replaying a write
            Log.w(
                    TAG,
                    attempted
                            ? "Activation publication result unknown after write attempt; no replay"
                            : "Activation fixture unavailable before publication",
                    e);
        } finally {
            if (data != null) {
                data.recycle();
            }

            synchronized (ActivationPublisher.class) {
                inFlight = false;
            }
        }
    }
}
