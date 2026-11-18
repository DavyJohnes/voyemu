package com.qinggan.canbus;

public interface ICanBusServiceCallback extends android.os.IInterface {
    void onAirConditionChanged(AirCondition p0) throws android.os.RemoteException;
}
