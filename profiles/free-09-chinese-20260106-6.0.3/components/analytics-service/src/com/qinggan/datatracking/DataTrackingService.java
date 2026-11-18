package com.qinggan.datatracking;

import android.app.Service;
import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;

/**
 * Stub service matching OEM DataTrackingService of the analytics app. Clients (AnalyticsProxy
 * inside OEM applications) bind to com.qinggan.datatracking.DataTrackingService and cast the binder
 * to IAnalyticsService (AIDL descriptor "com.qinggan.datatracking.serviceproxy.IAnalyticsService",
 * transactions sendMessage=1, sendGzipMessage=2, sendGzipMsgPath=3, all returning int). The stub
 * answers every transaction with 0 (accepted), so proxy retry loops stop immediately.
 */
public class DataTrackingService extends Service {
    private static final String DESCRIPTOR =
            "com.qinggan.datatracking.serviceproxy.IAnalyticsService";

    private static final class AnalyticsBinder extends Binder {
        @Override
        protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                throws RemoteException {
            switch (code) {
                case INTERFACE_TRANSACTION:
                    reply.writeString(DESCRIPTOR);

                    return true;
                case 1: // sendMessage
                case 2: // sendGzipMessage
                case 3: // sendGzipMsgPath
                    data.enforceInterface(DESCRIPTOR);
                    reply.writeNoException();
                    reply.writeInt(0);

                    return true;
                default:
                    return super.onTransact(code, data, reply, flags);
            }
        }
    }

    private final IBinder binder = new AnalyticsBinder();

    @Override
    public IBinder onBind(Intent intent) {
        return binder;
    }
}
