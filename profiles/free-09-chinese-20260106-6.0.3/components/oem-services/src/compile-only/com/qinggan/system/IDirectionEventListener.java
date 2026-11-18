package com.qinggan.system;

public interface IDirectionEventListener extends android.os.IInterface {
    void onAreaFocusChanged(AreaFocusModel p0, AreaFocusModel p1, android.view.KeyEvent p2)
            throws android.os.RemoteException;
}
