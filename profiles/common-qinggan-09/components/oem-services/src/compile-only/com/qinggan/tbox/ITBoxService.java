package com.qinggan.tbox;

public interface ITBoxService extends android.os.IInterface {
    abstract class Stub extends android.os.Binder implements ITBoxService {
        public Stub() {}

        @Override
        public android.os.IBinder asBinder() {
            return null;
        }
    }

    boolean getDatacomSwitchStatus() throws android.os.RemoteException;

    int getECallType() throws android.os.RemoteException;

    boolean getEFenceEnable() throws android.os.RemoteException;

    int getEFenceRange() throws android.os.RemoteException;

    int getEFenceSensitivity() throws android.os.RemoteException;

    String getNetworkOperator() throws android.os.RemoteException;

    int getNetworkStatus() throws android.os.RemoteException;

    int getNetworkType() throws android.os.RemoteException;

    String getPhoneNumber() throws android.os.RemoteException;

    int getSimCardStatus() throws android.os.RemoteException;

    int getTBoxCallStatus() throws android.os.RemoteException;

    int getTBoxCallType() throws android.os.RemoteException;

    String getTBoxDumpInfo() throws android.os.RemoteException;

    String getTBoxHWVersion() throws android.os.RemoteException;

    String getTBoxICCID() throws android.os.RemoteException;

    String getTBoxIMEI() throws android.os.RemoteException;

    String getTBoxIMSI() throws android.os.RemoteException;

    String getTBoxMCUVersion() throws android.os.RemoteException;

    String getTBoxMPUVersion() throws android.os.RemoteException;

    String getTBoxMSISDN() throws android.os.RemoteException;

    String getTBoxPDSN() throws android.os.RemoteException;

    int getTBoxSignalStrength() throws android.os.RemoteException;

    String getTBoxVIN() throws android.os.RemoteException;

    int getUpgradePercentage() throws android.os.RemoteException;

    int getUpgradeStatus() throws android.os.RemoteException;

    void hangupBCall() throws android.os.RemoteException;

    void hangupECall() throws android.os.RemoteException;

    void hangupICall() throws android.os.RemoteException;

    boolean isLianLuTBox() throws android.os.RemoteException;

    boolean isTBoxConnected() throws android.os.RemoteException;

    void rebootTBox() throws android.os.RemoteException;

    boolean registerListener(String p0, ITBoxListener p1) throws android.os.RemoteException;

    void requestBCall() throws android.os.RemoteException;

    void requestECall() throws android.os.RemoteException;

    void requestICall() throws android.os.RemoteException;

    void set5GSwitch(int p0) throws android.os.RemoteException;

    void setEFenceStatus(boolean p0, int p1, int p2) throws android.os.RemoteException;

    void setTimeZone(int p0) throws android.os.RemoteException;

    void startTBoxUpdate(String p0) throws android.os.RemoteException;

    void startTransferLogConfig(String p0) throws android.os.RemoteException;

    void startTransferTBoxLog() throws android.os.RemoteException;

    void switchDatacom(boolean p0) throws android.os.RemoteException;

    boolean unregisterListener(String p0, ITBoxListener p1) throws android.os.RemoteException;
}
