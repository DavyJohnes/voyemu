package com.qinggan.system;

public interface IKeyManagerCallback extends android.os.IInterface {
    boolean onEvent(QGKeyEvent p0, String p1, int p2) throws android.os.RemoteException;
}
