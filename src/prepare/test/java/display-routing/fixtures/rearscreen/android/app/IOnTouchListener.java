package android.app;

import android.os.IInterface;
import android.os.RemoteException;

public interface IOnTouchListener extends IInterface {
    void onTouchEvent(int action) throws RemoteException;
}
