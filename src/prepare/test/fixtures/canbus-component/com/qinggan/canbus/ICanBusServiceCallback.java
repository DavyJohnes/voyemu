package com.qinggan.canbus;

import android.os.Bundle;
import android.os.IInterface;
import android.os.RemoteException;

/** Callback ABI of the OEM CAN bus service; the signatures follow the donor DEX. */
public interface ICanBusServiceCallback extends IInterface {
    void onAccStateChanged(int p0) throws RemoteException;

    void onAirConditionChanged(AirCondition p0) throws RemoteException;

    void onAlarmDataChanged(int p0) throws RemoteException;

    void onAmbientTemperatureChanged(int p0) throws RemoteException;

    void onBatteryRemainingCapacityChanged(float p0) throws RemoteException;

    void onBatteryStateChanged(BatteryState p0) throws RemoteException;

    void onBrakeFluidStatusChanged(int p0) throws RemoteException;

    void onBrakePadStatusChanged(BrakePadStatus p0) throws RemoteException;

    void onCameraChanged(CameraState p0) throws RemoteException;

    void onCanBoxVersionChange(String p0) throws RemoteException;

    void onCanRawDataChanged(int p0, Bundle p1) throws RemoteException;

    void onCarKeyChanged(int p0, int p1) throws RemoteException;

    void onDVRStateChenaged(DVRState p0) throws RemoteException;

    void onDoorStatusChanged(DoorStatus p0) throws RemoteException;

    void onEnergyConsumptionInfoChanged(EnergyConsumptionInfo p0) throws RemoteException;

    void onEnergyConsumptionPercentChanged(EnergyConsumptionPercent p0) throws RemoteException;

    void onEngineFluidStatusChanged(int p0) throws RemoteException;

    void onEngineSpeedChanged(int p0) throws RemoteException;

    void onEngineStatusChanged(int p0) throws RemoteException;

    void onEngineTemperatureChanged(int p0) throws RemoteException;

    void onFactoryModeChanged(String p0) throws RemoteException;

    void onFuelConsumptionChanged(int p0) throws RemoteException;

    void onFuelLevelChanged(FuelLevel p0) throws RemoteException;

    void onGearStatusChanged(GearState p0) throws RemoteException;

    void onHEVSystemModelChanged(int p0) throws RemoteException;

    void onHandBrakeStatusChanged(int p0) throws RemoteException;

    void onICMReqCallModeChanged(boolean p0) throws RemoteException;

    void onICMReqCallStatusChanged(int p0) throws RemoteException;

    void onICMReqDialing(int p0) throws RemoteException;

    void onICMReqMediaChanged(int p0, int p1, int p2) throws RemoteException;

    void onICMReqMuteModeChanged(boolean p0) throws RemoteException;

    void onIlluminationChanged(int p0) throws RemoteException;

    void onInstantaneousFuelConsumptionChanged(float p0) throws RemoteException;

    void onLightStatusChanged(LightStatus p0) throws RemoteException;

    void onOdometerChanged(Odometer p0) throws RemoteException;

    void onParkStateChanged(int p0, int p1) throws RemoteException;

    void onPowerLevelChanged(PowerLevel p0) throws RemoteException;

    void onRadarDataChanged(RadarData p0) throws RemoteException;

    void onRainFallLevelChanged(int p0) throws RemoteException;

    void onReTrackingChanged(int p0) throws RemoteException;

    void onRealityWarningInfoChange(RealityWarningInfo p0) throws RemoteException;

    void onRealityWarningInfoChanged(int p0, int p1) throws RemoteException;

    void onSWCAngleChanged(SWCAngle p0) throws RemoteException;

    void onSeatAdjustStateChanged(SeatAdjustState p0) throws RemoteException;

    void onSeatBeltChanged(SeatBelt p0) throws RemoteException;

    void onSecondaryOdometerChanged(int p0) throws RemoteException;

    void onTPMSInfoChange(TPMSInfo p0) throws RemoteException;

    void onTravellingInfo(TravellingInfoType p0, float p1) throws RemoteException;

    void onVehicleCenterControlEnabledChanged(VehicleCenterControlEnabled p0, int p1)
            throws RemoteException;

    void onVehicleIOChanged(VehicleIO p0) throws RemoteException;

    void onVehicleKeyStateChanged(int p0) throws RemoteException;

    void onVehicleSceneModeChanged(int p0) throws RemoteException;

    void onVehicleSpeedChanged(int p0) throws RemoteException;

    void onVehicleStateChanged(VehicleState p0, int p1) throws RemoteException;

    void onVehicleStateSettingResponse(VehicleState p0, boolean p1) throws RemoteException;

    void onVehicleStateSettingResponseEx(VehicleState p0, int p1) throws RemoteException;

    void onWheelCountChanged(WheelCount p0) throws RemoteException;

    void onWheelSpeedChanged(WheelSpeed p0) throws RemoteException;

    void onWindowsStatusChanged(WindowStatus p0) throws RemoteException;

    void onWiperFluidStatusChanged(int p0) throws RemoteException;
}
