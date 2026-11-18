package com.qinggan.canbus;

public interface ICanBusService extends android.os.IInterface {
    abstract class Stub extends android.os.Binder implements ICanBusService {
        public Stub() {}

        @Override
        public android.os.IBinder asBinder() {
            return null;
        }
    }

    boolean addCallback(ICanBusServiceCallback p0) throws android.os.RemoteException;

    int getAccStatus() throws android.os.RemoteException;

    AirCondition getAirCondition() throws android.os.RemoteException;

    int getAirConditionState(AirConditionState p0) throws android.os.RemoteException;

    int getAlarmData() throws android.os.RemoteException;

    int getAmbientTemperature() throws android.os.RemoteException;

    float getBatteryRemainingCapacity() throws android.os.RemoteException;

    BatteryState getBatteryState() throws android.os.RemoteException;

    int getBrakeFluidStatus() throws android.os.RemoteException;

    BrakePadStatus getBrakePadStatus() throws android.os.RemoteException;

    CameraState getCameraState() throws android.os.RemoteException;

    String getCanBoxVersion() throws android.os.RemoteException;

    int[] getCanRawData(int p0) throws android.os.RemoteException;

    int getComprehensiveFuelConsumption() throws android.os.RemoteException;

    float getComprehensiveFuelConsumptionEX() throws android.os.RemoteException;

    DVRState getDVRState() throws android.os.RemoteException;

    DoorStatus getDoorStatus() throws android.os.RemoteException;

    int getEngineFluidStatus() throws android.os.RemoteException;

    int getEngineSpeed() throws android.os.RemoteException;

    int getEngineStatus() throws android.os.RemoteException;

    int getEngineTemperature() throws android.os.RemoteException;

    RadarData getFrontRadarData() throws android.os.RemoteException;

    FuelLevel getFuelLevel() throws android.os.RemoteException;

    GearState getGearStatus() throws android.os.RemoteException;

    int getHandBrakeStatus() throws android.os.RemoteException;

    int getHevSysMode() throws android.os.RemoteException;

    int getIllumination() throws android.os.RemoteException;

    int getInstantFuelConsumption() throws android.os.RemoteException;

    com.qinggan.ipksync.IIpkSyncService getIpkSyncSvr() throws android.os.RemoteException;

    RadarData getLeftRadarData() throws android.os.RemoteException;

    LightStatus getLightStatus() throws android.os.RemoteException;

    Odometer getOdometer() throws android.os.RemoteException;

    PowerLevel getPowerlLevel() throws android.os.RemoteException;

    int getRainfallLevel() throws android.os.RemoteException;

    int getReTracking() throws android.os.RemoteException;

    RealityWarningInfo getRealityWarningInfo() throws android.os.RemoteException;

    RadarData getRearRadarData() throws android.os.RemoteException;

    RadarData getRightRadarData() throws android.os.RemoteException;

    SWCAngle getSWCAngle() throws android.os.RemoteException;

    SeatBelt getSeatBeltStatus() throws android.os.RemoteException;

    long getSecondaryOdometer() throws android.os.RemoteException;

    TPMSInfo getTPMSInfo() throws android.os.RemoteException;

    int getVehicleCenterControlEnabled(VehicleCenterControlEnabled p0)
            throws android.os.RemoteException;

    VehicleIO getVehicleIO() throws android.os.RemoteException;

    int getVehicleKeyState() throws android.os.RemoteException;

    int getVehicleSceneMode() throws android.os.RemoteException;

    int getVehicleSpeed() throws android.os.RemoteException;

    int getVehicleState(VehicleState p0) throws android.os.RemoteException;

    WheelCount getWheelCount() throws android.os.RemoteException;

    WheelSpeed getWheelSpeed() throws android.os.RemoteException;

    WindowStatus getWindowStatus() throws android.os.RemoteException;

    int getWiperFluidStatus() throws android.os.RemoteException;

    boolean isSupportFunction(FunctionType p0) throws android.os.RemoteException;

    void queryVehicleState() throws android.os.RemoteException;

    boolean removeCallback(ICanBusServiceCallback p0) throws android.os.RemoteException;

    int requestTravellingInfo(TravellingInfoType p0) throws android.os.RemoteException;

    int requestVehicleCenterControlEnabledInfo() throws android.os.RemoteException;

    int setAirConditionBundleState(android.os.Bundle p0) throws android.os.RemoteException;

    void setAirConditionState(AirConditionState p0, int p1) throws android.os.RemoteException;

    void setCanRawData(int p0, int[] p1) throws android.os.RemoteException;

    void setFactoryModeResult(int p0, String p1) throws android.os.RemoteException;

    void setFactoryModeState(VehicleStateAutoTest p0, String p1, int p2)
            throws android.os.RemoteException;

    void setGearBoxType(GearBoxType p0) throws android.os.RemoteException;

    int setVehicleAndAirConditionBundleState(android.os.Bundle p0, android.os.Bundle p1)
            throws android.os.RemoteException;

    int setVehicleBundleState(android.os.Bundle p0) throws android.os.RemoteException;

    boolean setVehicleCenterControlEnabled(VehicleCenterControlEnabled p0, int p1)
            throws android.os.RemoteException;

    int setVehicleSceneMode(int p0) throws android.os.RemoteException;

    void setVehicleState(VehicleState p0, int p1) throws android.os.RemoteException;

    void updateCallListInfo(android.os.Bundle p0) throws android.os.RemoteException;

    void updateMediaPlayInfo(MediaPlayInfo p0) throws android.os.RemoteException;

    void updateMediaPlayMode(MediaPlayMode p0) throws android.os.RemoteException;

    void updateMediaPlayProgressInfo(MediaPlayProgressInfo p0) throws android.os.RemoteException;

    void updateMediaSeqType(int p0) throws android.os.RemoteException;

    void updateMediaSrcInfo(MediaSrcInfo p0) throws android.os.RemoteException;

    void updateMediaVolume(int p0) throws android.os.RemoteException;

    void updateNaviInfo(NaviInfo p0) throws android.os.RemoteException;

    void updateNaviLaneInfo(boolean p0, android.os.Bundle p1) throws android.os.RemoteException;

    void updatePhoneInfo(PhoneInfo p0) throws android.os.RemoteException;
}
