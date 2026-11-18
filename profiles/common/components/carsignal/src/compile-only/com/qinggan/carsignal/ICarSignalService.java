package com.qinggan.carsignal;

public interface ICarSignalService extends android.os.IInterface {
    abstract class Stub extends android.os.Binder implements ICarSignalService {
        public Stub() {}

        @Override
        public android.os.IBinder asBinder() {
            return null;
        }
    }

    void PowerOff() throws android.os.RemoteException;

    int getAVNStatus() throws android.os.RemoteException;

    int getAlarmState() throws android.os.RemoteException;

    int getAlarmTime() throws android.os.RemoteException;

    int getBatteryLevel() throws android.os.RemoteException;

    String getCanBoxVersion() throws android.os.RemoteException;

    int getCanBusState() throws android.os.RemoteException;

    int getCarKeyStatus() throws android.os.RemoteException;

    String getCarType() throws android.os.RemoteException;

    int getCarVideoStreamMode() throws android.os.RemoteException;

    boolean getCurrStateInScreenOffState() throws android.os.RemoteException;

    int getCurrentMBHighTempLevel() throws android.os.RemoteException;

    int getCurrentMBHighTempValue() throws android.os.RemoteException;

    int getDABAnt() throws android.os.RemoteException;

    int getFactoryModeKey() throws android.os.RemoteException;

    int getGPSAnt() throws android.os.RemoteException;

    int getHeadLampStatus() throws android.os.RemoteException;

    int getKeyBoardBackLightValue() throws android.os.RemoteException;

    int getKeyCtlState() throws android.os.RemoteException;

    int getLCDBoardLightValue() throws android.os.RemoteException;

    int getLCDTemp() throws android.os.RemoteException;

    int getLFDoorState() throws android.os.RemoteException;

    int getLightSensorLevel() throws android.os.RemoteException;

    int getLinBusState() throws android.os.RemoteException;

    String getMCUHardwareVersion() throws android.os.RemoteException;

    int getMCUPlatform() throws android.os.RemoteException;

    int getMCUPowerMode() throws android.os.RemoteException;

    String getMCUSoftVersionDate() throws android.os.RemoteException;

    String getMCUSoftwareVersion() throws android.os.RemoteException;

    String getMPUKernelVersion() throws android.os.RemoteException;

    int getMainSpeakState() throws android.os.RemoteException;

    int getPAMuteState() throws android.os.RemoteException;

    int getPASLCDTemp() throws android.os.RemoteException;

    int getPASLightSensorLevel() throws android.os.RemoteException;

    int getRTCState() throws android.os.RemoteException;

    long getRTCTime() throws android.os.RemoteException;

    int getRecoveryKey() throws android.os.RemoteException;

    int getReverseState() throws android.os.RemoteException;

    int getScreenLightStatus() throws android.os.RemoteException;

    int getShutdownMode() throws android.os.RemoteException;

    int getSideLightStatus() throws android.os.RemoteException;

    boolean getStandbyStateRestoreMode() throws android.os.RemoteException;

    int getTboxMutePinState() throws android.os.RemoteException;

    int getTimeModeState() throws android.os.RemoteException;

    int getTouchScreenState() throws android.os.RemoteException;

    int getTunerAnt() throws android.os.RemoteException;

    int[] getVehicleConfig() throws android.os.RemoteException;

    float getVehicleSpeed() throws android.os.RemoteException;

    int getVehicleType() throws android.os.RemoteException;

    int getVoltageState() throws android.os.RemoteException;

    int getWTDogState() throws android.os.RemoteException;

    int getWakeupSrc() throws android.os.RemoteException;

    int getWheelKeyChannelLine1Value() throws android.os.RemoteException;

    int getWheelKeyChannelLine2Value() throws android.os.RemoteException;

    boolean isInMainBoardHighTempMode() throws android.os.RemoteException;

    boolean registerCallback(ICarSignalServiceCallBack p0) throws android.os.RemoteException;

    void setAlarmState(int p0) throws android.os.RemoteException;

    void setAlarmTime(int p0) throws android.os.RemoteException;

    void setAudioSourceType(int p0) throws android.os.RemoteException;

    void setBTPhoneState(int p0) throws android.os.RemoteException;

    int setBtMacValue(int[] p0) throws android.os.RemoteException;

    void setDLSKeyguardStatus(boolean p0) throws android.os.RemoteException;

    void setKeyBoardBackLightValue(int p0) throws android.os.RemoteException;

    void setKeyCtlState(int p0) throws android.os.RemoteException;

    void setLCDBoardLightValue(int p0) throws android.os.RemoteException;

    void setMCUMusicDiag(int p0, int p1) throws android.os.RemoteException;

    void setMCUMusicDiagEx(com.qinggan.carsignal.diagnostic.DiagnosticInfo p0)
            throws android.os.RemoteException;

    void setMPUPowerMode(int[] p0) throws android.os.RemoteException;

    void setPAMuteState(int p0) throws android.os.RemoteException;

    void setRTCState(int p0) throws android.os.RemoteException;

    void setRTCTime(int p0) throws android.os.RemoteException;

    void setTimeModeState(int p0) throws android.os.RemoteException;

    void setTouchScreenState(int p0) throws android.os.RemoteException;

    int setVehicleConfig(int[] p0) throws android.os.RemoteException;

    void setVehicleVinCode(String p0) throws android.os.RemoteException;

    boolean unregisterCallback(ICarSignalServiceCallBack p0) throws android.os.RemoteException;
}
