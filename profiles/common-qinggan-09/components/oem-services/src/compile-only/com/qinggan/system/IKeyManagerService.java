package com.qinggan.system;

public interface IKeyManagerService extends android.os.IInterface {
    abstract class Stub extends android.os.Binder implements IKeyManagerService {
        public Stub() {}

        @Override
        public android.os.IBinder asBinder() {
            return null;
        }
    }

    boolean addFocusArea(AreaFocusModel p0, String p1) throws android.os.RemoteException;

    void focusAsSystem(boolean p0, String p1) throws android.os.RemoteException;

    int getMappingKeyEvent(int p0, String p1) throws android.os.RemoteException;

    void inputKeyEvent(int p0, int p1, String p2) throws android.os.RemoteException;

    void inputQGKeyEvent(QGKeyEvent p0, String p1) throws android.os.RemoteException;

    void mappingKeyEvent(int p0, int p1, String p2) throws android.os.RemoteException;

    boolean registerDirectionEventListener(IDirectionEventListener p0, String p1)
            throws android.os.RemoteException;

    int registerInterceptListener(
            android.os.IBinder p0, IKeyManagerCallback p1, int[] p2, String p3, int p4, int p5)
            throws android.os.RemoteException;

    int registerKeyManagerListener(
            android.os.IBinder p0, IKeyManagerCallback p1, int[] p2, String p3, int p4, int p5)
            throws android.os.RemoteException;

    boolean removeFocusArea(AreaFocusModel p0, String p1) throws android.os.RemoteException;

    boolean requestAreaFocus(AreaFocusModel p0, String p1) throws android.os.RemoteException;

    void setRestriction(boolean p0, int[] p1) throws android.os.RemoteException;

    boolean unregisterDirectionEventListener(IDirectionEventListener p0, String p1)
            throws android.os.RemoteException;

    int unregisterInterceptListener(IKeyManagerCallback p0, String p1)
            throws android.os.RemoteException;

    int unregisterKeyManagerListener(IKeyManagerCallback p0, String p1)
            throws android.os.RemoteException;

    void updateAreaFocus(int p0, int[] p1, String p2) throws android.os.RemoteException;
}
