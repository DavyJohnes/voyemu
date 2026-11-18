package android.os;

public class Binder implements IBinder {
    public static final int INTERFACE_TRANSACTION = 1598968902;

    protected void attachInterface(Object owner, String descriptor) {}

    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
            throws RemoteException {
        return false;
    }

    public boolean transact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
        return onTransact(code, data, reply, flags);
    }

    public void linkToDeath(DeathRecipient r, int flags) {}

    public boolean unlinkToDeath(DeathRecipient r, int flags) {
        return true;
    }
}
