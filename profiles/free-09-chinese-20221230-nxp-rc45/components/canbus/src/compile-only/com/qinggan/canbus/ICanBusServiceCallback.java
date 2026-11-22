package com.qinggan.canbus;

public interface ICanBusServiceCallback extends android.os.IInterface {
    void onAccStateChanged(int p0) throws android.os.RemoteException;

    void onAirConditionChanged(AirCondition p0) throws android.os.RemoteException;

    void onVehicleStateChanged(VehicleState p0, int p1) throws android.os.RemoteException;
}
