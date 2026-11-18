package com.qinggan.canbus.service.protocol.dongfeng;

import android.content.ContentResolver;
import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.util.Log;

import com.qinggan.canbus.AirConditionState;
import com.qinggan.canbus.BatteryState;
import com.qinggan.canbus.CameraState;
import com.qinggan.canbus.DFVehicleState;
import com.qinggan.canbus.DVRState;
import com.qinggan.canbus.DoorStatus;
import com.qinggan.canbus.FuelLevel;
import com.qinggan.canbus.GearState;
import com.qinggan.canbus.LightStatus;
import com.qinggan.canbus.MediaPlayProgressInfo;
import com.qinggan.canbus.MediaSrcInfo;
import com.qinggan.canbus.NaviInfo;
import com.qinggan.canbus.Odometer;
import com.qinggan.canbus.PhoneInfo;
import com.qinggan.canbus.PowerLevel;
import com.qinggan.canbus.TPMSInfo;
import com.qinggan.canbus.VehicleState;
import com.qinggan.canbus.WindowStatus;
import com.qinggan.canbus.service.BaseCanBusComponent;
import com.qinggan.canbus.service.CanModelBridge;
import com.qinggan.canbus.service.IIpkSyncComponentBase;

import java.io.FileDescriptor;
import java.io.PrintWriter;
import java.util.Locale;

/**
 * Dongfeng component of the emulator: the CAN model of the guest supplies the PW and FEV states and
 * the ACC through {@link CanModelBridge}; the other methods the OEM Dongfeng component declares
 * read and write the vehicle state the base component maintains.
 */
public class DongfengCanBusComponentImpl extends BaseCanBusComponent {
    private static final String TAG = "DongfengBusComponentImpl";
    private final CanModelBridge mModel = new CanModelBridge();

    private final CanModelBridge.Listener mModelListener =
            new CanModelBridge.Listener() {
                @Override
                public void onModelState(
                        CanModelBridge source,
                        VehicleState state,
                        int value,
                        long session,
                        long epoch,
                        long revision) {
                    handler()
                            .post(() -> {
                                if (source.isCurrent(session, epoch, revision)) {
                                    onVehicleStateChanged(state, value);
                                }
                            });
                }

                @Override
                public void onModelResponse(
                        CanModelBridge source,
                        VehicleState state,
                        int value,
                        long session,
                        long epoch) {
                    handler()
                            .post(() -> {
                                if (source.isCurrentEpoch(session, epoch)) {
                                    onVehicleStateSettingResponseEx(state, value);
                                }
                            });
                }

                @Override
                public void onModelAcc(CanModelBridge source, int value) {
                    handler()
                            .post(() -> {
                                if (source.getAccStatus() == value) {
                                    onAccStateChanged(value);
                                }
                            });
                }
            };

    private Handler handler() {
        return state().handler();
    }

    @Override
    public void init(Context context) {
        super.init(context);
    }

    @Override
    public void start() {
        mModel.start(mModelListener);
    }

    @Override
    public void stop() {
        mModel.stop();
    }

    // ------------------------------------------------------------------
    // ACC and the model states
    // ------------------------------------------------------------------

    /** Like the OEM component, reports the CAN ACC: OFF=0, ON=2, unknown -1. */
    @Override
    public int getAccStatus() {
        return mModel.getAccStatus();
    }

    /**
     * Requests head-unit power from the framework screen lifecycle; callbacks and getAccStatus
     * follow only the ACC the model then reports.
     */
    @Override
    public void setAccStatus(int value) {
        if (state().requestAcc(value)) {
            mModel.setAccStatus(value);
        }

        Log.i(TAG, "profile ACC request=" + value + "; Binder retained");
    }

    /** Journals and reports an observed ACC transition. */
    @Override
    public void onAccStateChanged(int value) {
        state().accApplied(value);
        super.onAccStateChanged(value);
    }

    @Override
    public int getVehicleState(VehicleState state) {
        return CanModelBridge.isModelState(state)
                ? mModel.getVehicleState(state)
                : super.getVehicleState(state);
    }

    @Override
    public void setVehicleState(VehicleState state, int value) {
        if (CanModelBridge.isModelState(state)) {
            state().recordWrite(state, value);
            mModel.setVehicleState(state, value);

            return;
        }

        super.setVehicleState(state, value);
    }

    @Override
    public void onVehicleStateSettingResponse(VehicleState state, boolean success) {
        super.onVehicleStateSettingResponse(state, success);
    }

    @Override
    public int setVehicleBundleState(Bundle bundle) {
        return super.setVehicleBundleState(bundle);
    }

    @Override
    public int setVehicleAndAirConditionBundleState(Bundle air, Bundle vehicle) {
        return super.setVehicleAndAirConditionBundleState(air, vehicle);
    }

    /**
     * Caches the persisted last value of a setting from QGSettings as the OEM component does; the
     * default applies when the guest has no QGSettings.
     */
    public int initVehicleLastStatus(ContentResolver resolver, VehicleState state, int value) {
        int last = value;

        try {
            last =
                    (Integer)
                            Class.forName("com.qinggan.provider.QGSettings$System")
                                    .getMethod(
                                            "getInt",
                                            ContentResolver.class,
                                            String.class,
                                            int.class)
                                    .invoke(null, resolver, state.toString(), value);
        } catch (ReflectiveOperationException absent) {
            Log.w(TAG, "QGSettings unavailable; " + state + " keeps " + value);
        }

        return state().initVehicleState(state, last);
    }

    // ------------------------------------------------------------------
    // Maintained vehicle state
    // ------------------------------------------------------------------

    @Override
    public int getAirConditionState(AirConditionState state) {
        return super.getAirConditionState(state);
    }

    @Override
    public void setAirConditionState(AirConditionState state, int value) {
        super.setAirConditionState(state, value);
    }

    @Override
    public float getBatteryRemainingCapacity() {
        return super.getBatteryRemainingCapacity();
    }

    @Override
    public BatteryState getBatteryState() {
        return super.getBatteryState();
    }

    @Override
    public CameraState getCameraState() {
        return super.getCameraState();
    }

    @Override
    public float getComprehensiveFuelConsumptionEX() {
        return super.getComprehensiveFuelConsumptionEX();
    }

    @Override
    public DVRState getDVRState() {
        return super.getDVRState();
    }

    @Override
    public DoorStatus getDoorStatus() {
        return super.getDoorStatus();
    }

    @Override
    public int getEngineTemperature() {
        return super.getEngineTemperature();
    }

    @Override
    public FuelLevel getFuelLevel() {
        return super.getFuelLevel();
    }

    @Override
    public GearState getGearStatus() {
        return super.getGearStatus();
    }

    @Override
    public int getInstantFuelConsumption() {
        return super.getInstantFuelConsumption();
    }

    @Override
    public LightStatus getLightStatus() {
        return super.getLightStatus();
    }

    @Override
    public Odometer getOdometer() {
        return super.getOdometer();
    }

    @Override
    public PowerLevel getPowerLevel() {
        return super.getPowerLevel();
    }

    @Override
    public TPMSInfo getTPMSInfo() {
        return super.getTPMSInfo();
    }

    @Override
    public WindowStatus getWindowStatus() {
        return super.getWindowStatus();
    }

    // ------------------------------------------------------------------
    // CAN box frames and the cluster link
    // ------------------------------------------------------------------

    /** Raw CAN frames of the CAN box; the emulator receives the vehicle from the model instead. */
    public void dispatchData(int id, int[] data) {
        Log.w(TAG, "raw CAN frame without CAN box decoder: " + id);
    }

    @Override
    public int[] getCanRawData(int id) {
        return super.getCanRawData(id);
    }

    @Override
    public void setCanRawData(int id, int[] data) {
        super.setCanRawData(id, data);
    }

    @Override
    public IIpkSyncComponentBase getIpkSyncComponent() {
        return super.getIpkSyncComponent();
    }

    /** Without the IPK sync link only the navigation state of the OEM component changes. */
    @Override
    public void sendIPKNavideath() {
        Log.d(TAG, "dongfengcanbus sendIPKNavideath");
        setVehicleState(DFVehicleState.IVI_AVM_NAVI_STATE, 2);
    }

    public void printData(String trace, int[] data) {
        StringBuilder text = new StringBuilder("[ ");

        for (int value : data) {
            text.append(String.format(Locale.ROOT, "%02X ", value));
        }

        Log.d(TAG, "print trace {" + trace + "} data: " + text.append(']'));
    }

    @Override
    public void dump(FileDescriptor fd, PrintWriter writer, String[] args) {
        super.dump(fd, writer, args);
        writer.println(
                "model PW="
                        + mModel.getVehicleState(VehicleState.HUM_VSP_FUNCTION_SW)
                        + " FEV="
                        + mModel.getVehicleState(VehicleState.IVI_SOC_MODESET));
    }

    // ------------------------------------------------------------------
    // Head-unit information for the cluster
    // ------------------------------------------------------------------

    @Override
    public void updatePhoneInfo(PhoneInfo info) {
        super.updatePhoneInfo(info);
    }

    @Override
    public void updateCallListInfo(Bundle info) {
        super.updateCallListInfo(info);
    }

    @Override
    public void updateMediaPlayProgressInfo(MediaPlayProgressInfo info) {
        super.updateMediaPlayProgressInfo(info);
    }

    @Override
    public void updateMediaSrcInfo(MediaSrcInfo info) {
        super.updateMediaSrcInfo(info);
    }

    @Override
    public void updateNaviInfo(NaviInfo info) {
        super.updateNaviInfo(info);
    }

    @Override
    public void updateNaviLaneInfo(boolean shown, Bundle info) {
        super.updateNaviLaneInfo(shown, info);
    }
}
