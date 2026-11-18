package com.voyemu.oem;

import com.qinggan.tbox.ITBoxListener;
import com.qinggan.tbox.ITBoxService;

/**
 * Emulator stub for the OEM dls_tbox_service binder (com.qinggan.tbox.ITBoxService).
 *
 * <p>The TBox (telematics unit) does not exist in the emulator: network getters report "no
 * service", identity getters return empty strings (they are private-data carriers and must not be
 * invented), calls and upgrade commands are recorded and ignored.
 */
public class TBoxStub extends ITBoxService.Stub {
    private static final String TAG = "VoyemuOemStub";

    @Override
    public boolean getDatacomSwitchStatus() {
        return false;
    }

    @Override
    public int getECallType() {
        return 0;
    }

    @Override
    public boolean getEFenceEnable() {
        return false;
    }

    @Override
    public int getEFenceRange() {
        return 0;
    }

    @Override
    public int getEFenceSensitivity() {
        return 0;
    }

    @Override
    public String getNetworkOperator() {
        return "";
    }

    @Override
    public int getNetworkStatus() {
        // 0 = no service in the QG contract.
        return 0;
    }

    @Override
    public int getNetworkType() {
        return 0;
    }

    @Override
    public String getPhoneNumber() {
        // Private data; never invent a number.
        return "";
    }

    @Override
    public int getSimCardStatus() {
        return 0;
    }

    @Override
    public int getTBoxCallStatus() {
        return 0;
    }

    @Override
    public int getTBoxCallType() {
        return 0;
    }

    @Override
    public String getTBoxDumpInfo() {
        return "emulator stub";
    }

    @Override
    public String getTBoxHWVersion() {
        return "";
    }

    @Override
    public String getTBoxICCID() {
        return "";
    }

    @Override
    public String getTBoxIMEI() {
        return "";
    }

    @Override
    public String getTBoxIMSI() {
        return "";
    }

    @Override
    public String getTBoxMCUVersion() {
        return "";
    }

    @Override
    public String getTBoxMPUVersion() {
        return "";
    }

    @Override
    public String getTBoxMSISDN() {
        return "";
    }

    @Override
    public String getTBoxPDSN() {
        return "";
    }

    @Override
    public int getTBoxSignalStrength() {
        return 0;
    }

    @Override
    public String getTBoxVIN() {
        // Identity data: real VINs must never appear in emulator output.
        return "";
    }

    @Override
    public int getUpgradePercentage() {
        return 0;
    }

    @Override
    public int getUpgradeStatus() {
        return 0;
    }

    @Override
    public void hangupBCall() {
        logIgnored("hangupBCall");
    }

    @Override
    public void hangupECall() {
        logIgnored("hangupECall");
    }

    @Override
    public void hangupICall() {
        logIgnored("hangupICall");
    }

    @Override
    public boolean isLianLuTBox() {
        return false;
    }

    @Override
    public boolean isTBoxConnected() {
        return false;
    }

    @Override
    public void rebootTBox() {
        logIgnored("rebootTBox");
    }

    @Override
    public boolean registerListener(String name, ITBoxListener listener) {
        // No TBox events exist to deliver; accept the registration.
        return true;
    }

    @Override
    public void requestBCall() {
        logIgnored("requestBCall");
    }

    @Override
    public void requestECall() {
        // Emergency call hardware is absent; never fake a call.
        logIgnored("requestECall");
    }

    @Override
    public void requestICall() {
        logIgnored("requestICall");
    }

    @Override
    public void set5GSwitch(int mode) {
        logIgnored("set5GSwitch");
    }

    @Override
    public void setEFenceStatus(boolean enable, int range, int sensitivity) {
        logIgnored("setEFenceStatus");
    }

    @Override
    public void setTimeZone(int zone) {
        logIgnored("setTimeZone");
    }

    @Override
    public void startTBoxUpdate(String path) {
        logIgnored("startTBoxUpdate");
    }

    @Override
    public void startTransferLogConfig(String path) {
        logIgnored("startTransferLogConfig");
    }

    @Override
    public void startTransferTBoxLog() {
        logIgnored("startTransferTBoxLog");
    }

    @Override
    public void switchDatacom(boolean enable) {
        logIgnored("switchDatacom");
    }

    @Override
    public boolean unregisterListener(String name, ITBoxListener listener) {
        return true;
    }

    private static void logIgnored(String what) {
        android.util.Log.i(TAG, "dls_tbox_service: " + what + " ignored (emulator)");
    }
}
