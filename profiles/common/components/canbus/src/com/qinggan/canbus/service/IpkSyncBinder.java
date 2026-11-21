package com.qinggan.canbus.service;

import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import android.util.Log;

import com.qinggan.ipksync.IIpkSyncService;

/**
 * QGAPI IIpkSyncService Binder endpoint of getIpkSyncSvr: syncMenuCardInfo writes the cluster cards
 * over the IPK link as the OEM IpkSyncComponent; the other operations send nothing.
 */
final class IpkSyncBinder extends Binder implements IIpkSyncService {
    static final String DESCRIPTOR = "com.qinggan.ipksync.IIpkSyncService";
    static final int SYNC_MENU_CARD_INFO = 45;
    private final IpkInfoSync ipk;

    IpkSyncBinder(IpkInfoSync ipk) {
        this.ipk = ipk;
        attachInterface(null, DESCRIPTOR);
    }

    @Override
    public IBinder asBinder() {
        return this;
    }

    @Override
    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
            throws RemoteException {
        if (code == INTERFACE_TRANSACTION) {
            reply.writeString(DESCRIPTOR);

            return true;
        }

        if (code < 1 || code > 48) {
            return super.onTransact(code, data, reply, flags);
        }

        data.enforceInterface(DESCRIPTOR);

        if (code == SYNC_MENU_CARD_INFO) {
            try {
                ipk.cards("canbus", ClusterCards.read(data));
            } catch (IllegalStateException rejected) {
                Log.w("CanBusStub", "cluster cards rejected: " + rejected.getMessage());
            }
        } else {
            Log.d("CanBusStub", "unsupported IPK sync operation " + code);
        }

        reply.writeNoException();

        // getAreaIndex, getWindowState and getMUStateformICM: -1, as IpkSyncManager without the
        // service.
        if (code == 31 || code == 32 || code == 40) {
            reply.writeInt(-1);
        }

        return true;
    }
}
