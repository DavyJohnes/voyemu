package com.qinggan.system;

public interface IPowerManagerService extends android.os.IInterface {
    abstract class Stub extends android.os.Binder implements IPowerManagerService {
        public Stub() {}

        @Override
        public android.os.IBinder asBinder() {
            return null;
        }
    }

    boolean abandonDisplayInStandby(int p0) throws android.os.RemoteException;

    boolean abandonDisplayInStandbyForScreen(int p0, int p1) throws android.os.RemoteException;

    void enterScreenOff() throws android.os.RemoteException;

    void enterScreenOffForScreen(int p0) throws android.os.RemoteException;

    void enterStandby() throws android.os.RemoteException;

    void exitScreenOff() throws android.os.RemoteException;

    void exitScreenOffForScreen(int p0) throws android.os.RemoteException;

    int getAVNStatus() throws android.os.RemoteException;

    int getAVNStatusForScreen(int p0) throws android.os.RemoteException;

    int getCarKeyStatus() throws android.os.RemoteException;

    int getCurrentMBHighTempLevel() throws android.os.RemoteException;

    int getLCDTemp() throws android.os.RemoteException;

    boolean isScreenOff() throws android.os.RemoteException;

    boolean isScreenOffForScreen(int p0) throws android.os.RemoteException;

    boolean registerListener(String p0, com.qinggan.power.IPowerManagerListener p1)
            throws android.os.RemoteException;

    boolean requestDisplayInStandby(int p0) throws android.os.RemoteException;

    boolean requestDisplayInStandbyForScreen(int p0, int p1) throws android.os.RemoteException;

    void switchToRunMode() throws android.os.RemoteException;

    boolean unregisterListener(String p0, com.qinggan.power.IPowerManagerListener p1)
            throws android.os.RemoteException;
}
