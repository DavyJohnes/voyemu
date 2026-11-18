package android.app;

import android.os.IInterface;
import android.os.RemoteException;

// Compile-only OEM touch listener that the rear screen app carries. Never merged into guest DEX.
public interface IOnTouchListener extends IInterface {
    void onTouchEvent(int action) throws RemoteException;
}
