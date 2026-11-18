package com.qinggan.canbus;

public interface ICanBusServiceCallback extends android.os.IInterface {
    void onAccStateChanged(int p0) throws android.os.RemoteException;

    void onAirConditionChanged(AirCondition p0) throws android.os.RemoteException;

    void onAlarmDataChanged(int p0) throws android.os.RemoteException;

    void onAmbientTemperatureChanged(int p0) throws android.os.RemoteException;

    void onBatteryRemainingCapacityChanged(float p0) throws android.os.RemoteException;

    void onBatteryStateChanged(BatteryState p0) throws android.os.RemoteException;

    void onBrakeFluidStatusChanged(int p0) throws android.os.RemoteException;

    void onBrakePadStatusChanged(BrakePadStatus p0) throws android.os.RemoteException;

    void onCameraChanged(CameraState p0) throws android.os.RemoteException;

    void onCanBoxVersionChange(String p0) throws android.os.RemoteException;

    void onCanRawDataChanged(int p0, android.os.Bundle p1) throws android.os.RemoteException;

    void onCarKeyChanged(int p0, int p1) throws android.os.RemoteException;

    void onDVRStateChenaged(DVRState p0) throws android.os.RemoteException;

    void onDoorStatusChanged(DoorStatus p0) throws android.os.RemoteException;

    void onEngineFluidStatusChanged(int p0) throws android.os.RemoteException;

    void onEngineSpeedChanged(int p0) throws android.os.RemoteException;

    void onEngineStatusChanged(int p0) throws android.os.RemoteException;

    void onEngineTemperatureChanged(int p0) throws android.os.RemoteException;

    void onFactoryModeChanged(String p0) throws android.os.RemoteException;

    void onFuelConsumptionChanged(int p0) throws android.os.RemoteException;

    void onFuelLevelChanged(FuelLevel p0) throws android.os.RemoteException;

    void onGearStatusChanged(GearState p0) throws android.os.RemoteException;

    void onHandBrakeStatusChanged(int p0) throws android.os.RemoteException;

    void onICMReqCallModeChanged(boolean p0) throws android.os.RemoteException;

    void onICMReqCallStatusChanged(int p0) throws android.os.RemoteException;

    void onICMReqDialing(int p0) throws android.os.RemoteException;

    void onICMReqMediaChanged(int p0, int p1, int p2) throws android.os.RemoteException;

    void onICMReqMuteModeChanged(boolean p0) throws android.os.RemoteException;

    void onIlluminationChanged(int p0) throws android.os.RemoteException;

    void onInstantaneousFuelConsumptionChanged(float p0) throws android.os.RemoteException;

    void onLightStatusChanged(LightStatus p0) throws android.os.RemoteException;

    void onOdometerChanged(Odometer p0) throws android.os.RemoteException;

    void onParkStateChanged(int p0, int p1) throws android.os.RemoteException;

    void onPowerLevelChanged(PowerLevel p0) throws android.os.RemoteException;

    void onRadarDataChanged(RadarData p0) throws android.os.RemoteException;

    void onReTrackingChanged(int p0) throws android.os.RemoteException;

    void onRealityWarningInfoChange(RealityWarningInfo p0) throws android.os.RemoteException;

    void onRealityWarningInfoChanged(int p0, int p1) throws android.os.RemoteException;

    void onSWCAngleChanged(SWCAngle p0) throws android.os.RemoteException;

    void onSeatAdjustStateChanged(SeatAdjustState p0) throws android.os.RemoteException;

    void onSeatBeltChanged(SeatBelt p0) throws android.os.RemoteException;

    void onSecondaryOdometerChanged(int p0) throws android.os.RemoteException;

    void onTPMSInfoChange(TPMSInfo p0) throws android.os.RemoteException;

    void onTravellingInfo(TravellingInfoType p0, float p1) throws android.os.RemoteException;

    void onVehicleCenterControlEnabledChanged(VehicleCenterControlEnabled p0, int p1)
            throws android.os.RemoteException;

    void onVehicleIOChanged(VehicleIO p0) throws android.os.RemoteException;

    void onVehicleKeyStateChanged(int p0) throws android.os.RemoteException;

    void onVehicleSceneModeChanged(int p0) throws android.os.RemoteException;

    void onVehicleSpeedChanged(int p0) throws android.os.RemoteException;

    void onVehicleStateChanged(VehicleState p0, int p1) throws android.os.RemoteException;

    void onVehicleStateSettingResponse(VehicleState p0, boolean p1)
            throws android.os.RemoteException;

    void onVehicleStateSettingResponseEx(VehicleState p0, int p1) throws android.os.RemoteException;

    void onWheelCountChanged(WheelCount p0) throws android.os.RemoteException;

    void onWheelSpeedChanged(WheelSpeed p0) throws android.os.RemoteException;

    void onWindowsStatusChanged(WindowStatus p0) throws android.os.RemoteException;

    void onWiperFluidStatusChanged(int p0) throws android.os.RemoteException;
}
