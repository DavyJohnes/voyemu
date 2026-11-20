package com.qinggan.power;

public interface IPowerManagerListener extends android.os.IInterface {
    void onDoubleScreenStatusChange(int p0, int p1) throws android.os.RemoteException;
}
