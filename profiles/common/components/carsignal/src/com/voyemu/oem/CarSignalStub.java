package com.voyemu.oem;

/**
 * Emulator stub for the OEM qg.carsignal binder (com.qinggan.carsignal.ICarSignalService).
 *
 * <p>The vehicle CAN bus does not exist in the emulator: getters answer with a neutral parked
 * vehicle, setters have no effect, callbacks never fire.
 */
public class CarSignalStub extends com.qinggan.carsignal.ICarSignalService.Stub {
    @Override
    public void PowerOff() {
        android.util.Log.i("VoyemuOemStub", "qg.carsignal: PowerOff ignored (emulator)");
    }

    @Override
    public int getAVNStatus() {
        return 0;
    }

    @Override
    public int getAlarmState() {
        return 0;
    }

    @Override
    public int getAlarmTime() {
        return 0;
    }

    @Override
    public int getBatteryLevel() {
        return 0;
    }

    @Override
    public java.lang.String getCanBoxVersion() {
        return "";
    }

    @Override
    public int getCanBusState() {
        return 0;
    }

    @Override
    public int getCarKeyStatus() {
        return 0;
    }

    @Override
    public java.lang.String getCarType() {
        return "";
    }

    @Override
    public int getCarVideoStreamMode() {
        return 0;
    }

    @Override
    public boolean getCurrStateInScreenOffState() {
        return false;
    }

    @Override
    public int getCurrentMBHighTempLevel() {
        return 0;
    }

    @Override
    public int getCurrentMBHighTempValue() {
        return 0;
    }

    @Override
    public int getDABAnt() {
        return 0;
    }

    @Override
    public int getFactoryModeKey() {
        return 0;
    }

    @Override
    public int getGPSAnt() {
        return 0;
    }

    @Override
    public int getHeadLampStatus() {
        return 0;
    }

    @Override
    public int getKeyBoardBackLightValue() {
        return 0;
    }

    @Override
    public int getKeyCtlState() {
        return 0;
    }

    @Override
    public int getLCDBoardLightValue() {
        return 0;
    }

    @Override
    public int getLCDTemp() {
        return 0;
    }

    @Override
    public int getLFDoorState() {
        return 0;
    }

    @Override
    public int getLightSensorLevel() {
        return 0;
    }

    @Override
    public int getLinBusState() {
        return 0;
    }

    @Override
    public java.lang.String getMCUHardwareVersion() {
        return android.os.Build.VERSION.SDK_INT >= 30 ? "V1.0" : "V3.0";
    }

    @Override
    public int getMCUPlatform() {
        return 0;
    }

    @Override
    public int getMCUPowerMode() {
        return 0;
    }

    @Override
    public java.lang.String getMCUSoftVersionDate() {
        return "";
    }

    @Override
    public java.lang.String getMCUSoftwareVersion() {
        return "";
    }

    @Override
    public java.lang.String getMPUKernelVersion() {
        return "";
    }

    @Override
    public int getMainSpeakState() {
        return 0;
    }

    @Override
    public int getPAMuteState() {
        return 0;
    }

    @Override
    public int getPASLCDTemp() {
        return 0;
    }

    @Override
    public int getPASLightSensorLevel() {
        return 0;
    }

    @Override
    public int getRTCState() {
        return 0;
    }

    @Override
    public long getRTCTime() {
        return System.currentTimeMillis() / 1000;
    }

    @Override
    public int getRecoveryKey() {
        return 0;
    }

    @Override
    public int getReverseState() {
        return 0;
    }

    @Override
    public int getScreenLightStatus() {
        return 0;
    }

    @Override
    public int getShutdownMode() {
        return 0;
    }

    @Override
    public int getSideLightStatus() {
        return 0;
    }

    @Override
    public boolean getStandbyStateRestoreMode() {
        return false;
    }

    @Override
    public int getTboxMutePinState() {
        return 0;
    }

    @Override
    public int getTimeModeState() {
        return 0;
    }

    @Override
    public int getTouchScreenState() {
        return 0;
    }

    @Override
    public int getTunerAnt() {
        return 0;
    }

    @Override
    public int[] getVehicleConfig() {
        return new int[0];
    }

    @Override
    public float getVehicleSpeed() {
        return 0.0f;
    }

    @Override
    public int getVehicleType() {
        return 0;
    }

    @Override
    public int getVoltageState() {
        return 0;
    }

    @Override
    public int getWTDogState() {
        return 0;
    }

    @Override
    public int getWakeupSrc() {
        return 0;
    }

    @Override
    public int getWheelKeyChannelLine1Value() {
        return 0;
    }

    @Override
    public int getWheelKeyChannelLine2Value() {
        return 0;
    }

    @Override
    public boolean isInMainBoardHighTempMode() {
        return false;
    }

    @Override
    public boolean registerCallback(com.qinggan.carsignal.ICarSignalServiceCallBack p0) {
        return true;
    }

    @Override
    public void setAlarmState(int p0) {
        // No vehicle bus in the emulator; command has no effect.
    }

    @Override
    public void setAlarmTime(int p0) {
        // No vehicle bus in the emulator; command has no effect.
    }

    @Override
    public void setAudioSourceType(int p0) {
        // No vehicle bus in the emulator; command has no effect.
    }

    @Override
    public void setBTPhoneState(int p0) {
        // No vehicle bus in the emulator; command has no effect.
    }

    @Override
    public int setBtMacValue(int[] p0) {
        return 0;
    }

    @Override
    public void setDLSKeyguardStatus(boolean p0) {
        // No vehicle bus in the emulator; command has no effect.
    }

    @Override
    public void setKeyBoardBackLightValue(int p0) {
        // No vehicle bus in the emulator; command has no effect.
    }

    @Override
    public void setKeyCtlState(int p0) {
        // No vehicle bus in the emulator; command has no effect.
    }

    @Override
    public void setLCDBoardLightValue(int p0) {
        // No vehicle bus in the emulator; command has no effect.
    }

    @Override
    public void setMCUMusicDiag(int p0, int p1) {
        // No vehicle bus in the emulator; command has no effect.
    }

    @Override
    public void setMCUMusicDiagEx(com.qinggan.carsignal.diagnostic.DiagnosticInfo diag) {
        // No vehicle bus in the emulator; command has no effect.
    }

    @Override
    public void setMPUPowerMode(int[] p0) {
        // No vehicle bus in the emulator; command has no effect.
    }

    @Override
    public void setPAMuteState(int p0) {
        // No vehicle bus in the emulator; command has no effect.
    }

    @Override
    public void setRTCState(int p0) {
        // No vehicle bus in the emulator; command has no effect.
    }

    @Override
    public void setRTCTime(int p0) {
        // No vehicle bus in the emulator; command has no effect.
    }

    @Override
    public void setTimeModeState(int p0) {
        // No vehicle bus in the emulator; command has no effect.
    }

    @Override
    public void setTouchScreenState(int p0) {
        // No vehicle bus in the emulator; command has no effect.
    }

    @Override
    public int setVehicleConfig(int[] p0) {
        return 0;
    }

    @Override
    public void setVehicleVinCode(java.lang.String p0) {
        // No vehicle bus in the emulator; command has no effect.
    }

    @Override
    public boolean unregisterCallback(com.qinggan.carsignal.ICarSignalServiceCallBack p0) {
        return true;
    }
}
