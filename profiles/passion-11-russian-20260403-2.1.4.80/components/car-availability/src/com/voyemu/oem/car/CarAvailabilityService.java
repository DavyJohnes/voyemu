package com.voyemu.oem.car;

import android.app.Service;
import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;

/** Exposes availability without pretending to implement vehicle, power or audio services. */
public final class CarAvailabilityService extends Service {
    private static final String DESCRIPTOR = "android.car.ICar";
    private static final String SERVICE = "car_service";
    // Bound to the exact API30 owner interface in component.json, not a generic Android version.
    private static final int GET_CAR_SERVICE = 12;
    private final Binder binder =
            new Binder() {
                {
                    attachInterface(null, DESCRIPTOR);
                }

                @Override
                protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                        throws RemoteException {
                    if (reply == null || (flags & IBinder.FLAG_ONEWAY) != 0) {
                        return false;
                    }

                    if (code == IBinder.INTERFACE_TRANSACTION) {
                        reply.writeString(DESCRIPTOR);

                        return true;
                    }

                    if (code != GET_CAR_SERVICE) {
                        return super.onTransact(code, data, reply, flags);
                    }

                    data.enforceInterface(DESCRIPTOR);
                    data.readString();

                    if (data.dataAvail() != 0) {
                        throw new SecurityException("Unexpected ICar request data");
                    }

                    reply.writeNoException();
                    reply.writeStrongBinder(null);

                    return true;
                }
            };

    @Override
    public void onCreate() {
        super.onCreate();

        try {
            Class.forName("android.os.ServiceManager")
                    .getDeclaredMethod("addService", String.class, IBinder.class)
                    .invoke(null, SERVICE, binder);
        } catch (ReflectiveOperationException error) {
            throw new IllegalStateException("Cannot publish car availability", error);
        }
    }

    @Override
    public IBinder onBind(Intent intent) {
        return intent != null && DESCRIPTOR.equals(intent.getAction()) ? binder : null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_NOT_STICKY;
    }
}
