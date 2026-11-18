package com.qinggan.canbus.service;

import android.content.Context;
import android.os.Bundle;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;

import com.qinggan.canbus.AirCondition;
import com.qinggan.canbus.AirConditionState;
import com.qinggan.canbus.BatteryState;
import com.qinggan.canbus.BrakePadStatus;
import com.qinggan.canbus.CameraState;
import com.qinggan.canbus.DVRState;
import com.qinggan.canbus.DoorStatus;
import com.qinggan.canbus.EnergyConsumptionInfo;
import com.qinggan.canbus.EnergyConsumptionPercent;
import com.qinggan.canbus.FuelLevel;
import com.qinggan.canbus.FunctionType;
import com.qinggan.canbus.GearBoxType;
import com.qinggan.canbus.GearState;
import com.qinggan.canbus.ICanBusServiceCallback;
import com.qinggan.canbus.LightStatus;
import com.qinggan.canbus.MediaPlayInfo;
import com.qinggan.canbus.MediaPlayMode;
import com.qinggan.canbus.MediaPlayProgressInfo;
import com.qinggan.canbus.MediaSrcInfo;
import com.qinggan.canbus.NaviInfo;
import com.qinggan.canbus.Odometer;
import com.qinggan.canbus.PhoneInfo;
import com.qinggan.canbus.PowerLevel;
import com.qinggan.canbus.RadarData;
import com.qinggan.canbus.RealityWarningInfo;
import com.qinggan.canbus.SWCAngle;
import com.qinggan.canbus.SeatAdjustState;
import com.qinggan.canbus.SeatBelt;
import com.qinggan.canbus.TPMSInfo;
import com.qinggan.canbus.TravellingInfoType;
import com.qinggan.canbus.VehicleCenterControlEnabled;
import com.qinggan.canbus.VehicleIO;
import com.qinggan.canbus.VehicleState;
import com.qinggan.canbus.VehicleStateAutoTest;
import com.qinggan.canbus.WheelCount;
import com.qinggan.canbus.WheelSpeed;
import com.qinggan.canbus.WindowStatus;

import java.io.FileDescriptor;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * OEM CAN bus component API: it keeps the observers of the Binder service, dispatches the observed
 * vehicle changes to them and reads the vehicle state the component maintains in {@link
 * CanBusState}. Raw CAN and MCU frames have no CAN box in the emulator; the transport methods use a
 * registered {@link ICanBusOperator} as the OEM component does and report its absence the same way.
 */
public class BaseCanBusComponent {
    public static final int LANG_CN = 0;
    public static final int LANG_EN = 1;
    public static final int LANG_UNKNOWN = -1;
    public static final String TAG = "BaseCanBusComponent";
    private final Map<IBinder, ICanBusServiceCallback> mObservers = new HashMap<>();
    private final Map<IBinder, IBinder.DeathRecipient> mDeaths = new HashMap<>();
    private volatile CanBusState mState;
    private volatile ICanBusOperator mOperator;
    private volatile int mCanFd;
    private volatile int mMuFd;

    /** One callback delivery to an observer. */
    interface ObserverCall {
        void deliver(ICanBusServiceCallback observer) throws RemoteException;
    }

    // ------------------------------------------------------------------
    // Voyemu lifecycle and state
    // ------------------------------------------------------------------

    /** Returns the maintained vehicle state; init(Context) creates it. */
    public final CanBusState state() {
        return mState;
    }

    /** Starts the vehicle model of the component; the base component has none. */
    public void start() {}

    /** Stops the vehicle model of the component; the maintained state remains. */
    public void stop() {}

    /** Requests a head-unit power state from the screen lifecycle; the base has no model. */
    public void setAccStatus(int value) {}

    public void init(Context context) {
        synchronized (mObservers) {
            if (mState == null) {
                mState = new CanBusState(this, context);
            }
        }
    }

    public void destroy() {}

    public void dump(FileDescriptor fd, PrintWriter writer, String[] args) {
        int observers;

        synchronized (mObservers) {
            observers = mObservers.size();
        }

        writer.println("observers=" + observers + " acc=" + getAccStatus());
    }

    // ------------------------------------------------------------------
    // Observers
    // ------------------------------------------------------------------

    public synchronized boolean attachObserver(final ICanBusServiceCallback observer) {
        if (observer == null) {
            return false;
        }

        final IBinder binder = observer.asBinder();

        synchronized (mObservers) {
            if (mDeaths.containsKey(binder)) {
                return true;
            }

            IBinder.DeathRecipient death =
                    new IBinder.DeathRecipient() {
                        @Override
                        public void binderDied() {
                            synchronized (mObservers) {
                                if (mDeaths.get(binder) != this) {
                                    return;
                                }

                                mObservers.remove(binder);
                                mDeaths.remove(binder);
                                mState.recordCallback(
                                        "callback_death", null, null, null, null, binder);
                            }
                        }
                    };

            try {
                binder.linkToDeath(death, 0);
            } catch (RemoteException e) {
                return false;
            }

            mDeaths.put(binder, death);
            mObservers.put(binder, observer);
            mState.recordCallback("callback_registration", null, null, null, null, binder);
        }

        mState.observerAttached(observer);

        return true;
    }

    public boolean detachObserver(ICanBusServiceCallback observer) {
        if (observer == null) {
            return false;
        }

        synchronized (mObservers) {
            IBinder binder = observer.asBinder();
            IBinder.DeathRecipient death = mDeaths.remove(binder);

            if (death != null) {
                binder.unlinkToDeath(death, 0);
            }

            boolean removed = mObservers.remove(binder) != null;

            if (removed) {
                mState.recordCallback("callback_removal", null, null, null, null, binder);
            }

            return removed;
        }
    }

    /** True while the observer is the registered callback of its binder. */
    final boolean isObserver(ICanBusServiceCallback observer) {
        synchronized (mObservers) {
            return mObservers.get(observer.asBinder()) == observer;
        }
    }

    /** Delivers to every registered observer; a failed observer does not stop the others. */
    final void dispatch(String method, ObserverCall call) {
        List<ICanBusServiceCallback> observers;

        synchronized (mObservers) {
            observers = new ArrayList<>(mObservers.values());
        }

        for (ICanBusServiceCallback observer : observers) {
            if (!isObserver(observer)) {
                continue;
            }

            try {
                call.deliver(observer);
            } catch (RemoteException failure) {
                Log.w(TAG, method + " delivery failed", failure);
            }
        }
    }

    /** Delivers to every registered observer and journals each delivery. */
    final void dispatchJournaled(String method, String field, int raw, ObserverCall call) {
        List<ICanBusServiceCallback> observers;

        synchronized (mObservers) {
            observers = new ArrayList<>(mObservers.values());
        }

        for (ICanBusServiceCallback observer : observers) {
            if (!isObserver(observer)) {
                continue;
            }

            try {
                deliverJournaled(observer, method, field, raw, call);
            } catch (RemoteException failure) {
                Log.w(TAG, method + " delivery failed", failure);
            }
        }
    }

    /** Delivers to one observer and journals the dispatch, its result and the write it answers. */
    final void deliverJournaled(
            ICanBusServiceCallback observer,
            String method,
            String field,
            int raw,
            ObserverCall call)
            throws RemoteException {
        String write = mState.deliveringWrite();
        IBinder binder = observer.asBinder();

        mState.recordCallback("feedback_dispatch", write, field, raw, method, binder);

        try {
            call.deliver(observer);
            mState.recordCallback("feedback_sent", write, field, raw, method, binder);
        } catch (RemoteException failure) {
            mState.recordCallback("feedback_failed", write, field, raw, method, binder);
            throw failure;
        }
    }

    // ------------------------------------------------------------------
    // Observed changes
    // ------------------------------------------------------------------

    public void onAccStateChanged(int value) {
        dispatchJournaled("onAccStateChanged", "acc", value, o -> o.onAccStateChanged(value));
    }

    public void onAirConditionChanged(AirCondition value) {
        dispatch("onAirConditionChanged", o -> o.onAirConditionChanged(value));
    }

    public void onAlarmDataChanged(int value) {
        dispatch("onAlarmDataChanged", o -> o.onAlarmDataChanged(value));
    }

    public void onAmbientTemperatureChanged(int value) {
        dispatch("onAmbientTemperatureChanged", o -> o.onAmbientTemperatureChanged(value));
    }

    public void onBatteryRemainingCapacityChanged(float value) {
        dispatch(
                "onBatteryRemainingCapacityChanged",
                o -> o.onBatteryRemainingCapacityChanged(value));
    }

    public void onBatteryStateChanged(BatteryState value) {
        dispatch("onBatteryStateChanged", o -> o.onBatteryStateChanged(value));
    }

    public void onBrakeFluidStatusChanged(int value) {
        dispatch("onBrakeFluidStatusChanged", o -> o.onBrakeFluidStatusChanged(value));
    }

    public void onBrakePadStatusChanged(BrakePadStatus value) {
        dispatch("onBrakePadStatusChanged", o -> o.onBrakePadStatusChanged(value));
    }

    /** Raw CAN bus states come from the CAN box; the emulator has none to handle. */
    public boolean onCANBusState(int state) {
        return false;
    }

    public void onCameraChanged(CameraState value) {
        dispatch("onCameraChanged", o -> o.onCameraChanged(value));
    }

    public void onCanBoxVersionChange(String value) {
        dispatch("onCanBoxVersionChange", o -> o.onCanBoxVersionChange(value));
    }

    public void onCanRawDataChanged(int id, Bundle value) {
        dispatch("onCanRawDataChanged", o -> o.onCanRawDataChanged(id, value));
    }

    public void onCarKeyChanged(int key, int value) {
        dispatch("onCarKeyChanged", o -> o.onCarKeyChanged(key, value));
    }

    public void onDVRStateChenaged(DVRState value) {
        dispatch("onDVRStateChenaged", o -> o.onDVRStateChenaged(value));
    }

    public void onDoorStatusChanged(DoorStatus value) {
        dispatch("onDoorStatusChanged", o -> o.onDoorStatusChanged(value));
    }

    public void onEnergyConsumptionInfoChanged(EnergyConsumptionInfo value) {
        dispatch("onEnergyConsumptionInfoChanged", o -> o.onEnergyConsumptionInfoChanged(value));
    }

    public void onEnergyConsumptionPercentChanged(EnergyConsumptionPercent value) {
        dispatch(
                "onEnergyConsumptionPercentChanged",
                o -> o.onEnergyConsumptionPercentChanged(value));
    }

    public void onEngineFluidStatusChanged(int value) {
        dispatch("onEngineFluidStatusChanged", o -> o.onEngineFluidStatusChanged(value));
    }

    public void onEngineSpeedChanged(int value) {
        dispatch("onEngineSpeedChanged", o -> o.onEngineSpeedChanged(value));
    }

    public void onEngineStatusChanged(int value) {
        dispatch("onEngineStatusChanged", o -> o.onEngineStatusChanged(value));
    }

    public void onEngineTemperatureChanged(int value) {
        dispatch("onEngineTemperatureChanged", o -> o.onEngineTemperatureChanged(value));
    }

    public void onFactoryModeChangedNative(String value) {
        dispatch("onFactoryModeChanged", o -> o.onFactoryModeChanged(value));
    }

    public void onFuelConsumptionChanged(int value) {
        dispatch("onFuelConsumptionChanged", o -> o.onFuelConsumptionChanged(value));
    }

    public void onFuelLevelChanged(FuelLevel value) {
        dispatch("onFuelLevelChanged", o -> o.onFuelLevelChanged(value));
    }

    public void onGearStatusChanged(GearState value) {
        dispatch("onGearStatusChanged", o -> o.onGearStatusChanged(value));
    }

    /** As in the OEM component, the HEV system mode is not forwarded to observers. */
    public void onHEVSystemModelChanged(int value) {}

    public void onHandBrakeStatusChanged(int value) {
        dispatch("onHandBrakeStatusChanged", o -> o.onHandBrakeStatusChanged(value));
    }

    public void onICMReqCallModeChanged(boolean value) {
        dispatch("onICMReqCallModeChanged", o -> o.onICMReqCallModeChanged(value));
    }

    public void onICMReqCallStatusChanged(int value) {
        dispatch("onICMReqCallStatusChanged", o -> o.onICMReqCallStatusChanged(value));
    }

    public void onICMReqDialing(int value) {
        dispatch("onICMReqDialing", o -> o.onICMReqDialing(value));
    }

    public void onICMReqMediaChanged(int source, int action, int value) {
        dispatch("onICMReqMediaChanged", o -> o.onICMReqMediaChanged(source, action, value));
    }

    public void onICMReqMuteModeChanged(boolean value) {
        dispatch("onICMReqMuteModeChanged", o -> o.onICMReqMuteModeChanged(value));
    }

    public void onIlluminationChanged(int value) {
        dispatch("onIlluminationChanged", o -> o.onIlluminationChanged(value));
    }

    public void onInstantaneousFuelConsumptionChanged(float value) {
        dispatch(
                "onInstantaneousFuelConsumptionChanged",
                o -> o.onInstantaneousFuelConsumptionChanged(value));
    }

    public void onLightStatusChanged(LightStatus value) {
        dispatch("onLightStatusChanged", o -> o.onLightStatusChanged(value));
    }

    public void onOdometerChanged(Odometer value) {
        dispatch("onOdometerChanged", o -> o.onOdometerChanged(value));
    }

    public void onParkStateChanged(int state, int value) {
        dispatch("onParkStateChanged", o -> o.onParkStateChanged(state, value));
    }

    public void onPowerLevelChanged(PowerLevel value) {
        dispatch("onPowerLevelChanged", o -> o.onPowerLevelChanged(value));
    }

    public void onRadarDataChanged(RadarData value) {
        dispatch("onRadarDataChanged", o -> o.onRadarDataChanged(value));
    }

    public void onReTrackingChanged(int value) {
        dispatch("onReTrackingChanged", o -> o.onReTrackingChanged(value));
    }

    public void onRealityWarningInfoChange(RealityWarningInfo value) {
        dispatch("onRealityWarningInfoChange", o -> o.onRealityWarningInfoChange(value));
    }

    public void onRealityWarningInfoChanged(int item, int value) {
        dispatch("onRealityWarningInfoChanged", o -> o.onRealityWarningInfoChanged(item, value));
    }

    public void onSWCAngleChanged(SWCAngle value) {
        dispatch("onSWCAngleChanged", o -> o.onSWCAngleChanged(value));
    }

    public void onSeatAdjustStateChanged(SeatAdjustState value) {
        dispatch("onSeatAdjustStateChanged", o -> o.onSeatAdjustStateChanged(value));
    }

    public void onSeatBeltChanged(SeatBelt value) {
        dispatch("onSeatBeltChanged", o -> o.onSeatBeltChanged(value));
    }

    public void onSecondaryOdometerChanged(int value) {
        dispatch("onSecondaryOdometerChanged", o -> o.onSecondaryOdometerChanged(value));
    }

    public void onTPMSInfoChanged(TPMSInfo value) {
        dispatch("onTPMSInfoChange", o -> o.onTPMSInfoChange(value));
    }

    public void onTravellingInfo(TravellingInfoType type, float value) {
        dispatch("onTravellingInfo", o -> o.onTravellingInfo(type, value));
    }

    public void onVehicleCenterControlEnabledChanged(VehicleCenterControlEnabled item, int value) {
        dispatch(
                "onVehicleCenterControlEnabledChanged",
                o -> o.onVehicleCenterControlEnabledChanged(item, value));
    }

    public void onVehicleIOChanged(VehicleIO value) {
        dispatch("onVehicleIOChanged", o -> o.onVehicleIOChanged(value));
    }

    public void onVehicleKeyStateChanged(int value) {
        dispatch("onVehicleKeyStateChanged", o -> o.onVehicleKeyStateChanged(value));
    }

    public void onVehicleSceneModeChanged(int value) {
        dispatch("onVehicleSceneModeChanged", o -> o.onVehicleSceneModeChanged(value));
    }

    public void onVehicleSpeedChanged(int value) {
        dispatch("onVehicleSpeedChanged", o -> o.onVehicleSpeedChanged(value));
    }

    public void onVehicleStateChanged(VehicleState state, int value) {
        dispatchJournaled(
                "onVehicleStateChanged",
                "vehicle_state." + state.name(),
                value,
                o -> o.onVehicleStateChanged(state, value));
    }

    public void onVehicleStateSettingResponse(VehicleState state, boolean success) {
        dispatchJournaled(
                "onVehicleStateSettingResponse",
                "vehicle_state." + state.name(),
                success ? 1 : 0,
                o -> o.onVehicleStateSettingResponse(state, success));
    }

    public void onVehicleStateSettingResponseEx(VehicleState state, int value) {
        dispatchJournaled(
                "onVehicleStateSettingResponseEx",
                "vehicle_state." + state.name(),
                value,
                o -> o.onVehicleStateSettingResponseEx(state, value));
    }

    public void onWheelCountChanged(WheelCount value) {
        dispatch("onWheelCountChanged", o -> o.onWheelCountChanged(value));
    }

    public void onWheelSpeedChanged(WheelSpeed value) {
        dispatch("onWheelSpeedChanged", o -> o.onWheelSpeedChanged(value));
    }

    public void onWindowsStatusChanged(WindowStatus value) {
        dispatch("onWindowsStatusChanged", o -> o.onWindowsStatusChanged(value));
    }

    public void onWiperFluidStatusChanged(int value) {
        dispatch("onWiperFluidStatusChanged", o -> o.onWiperFluidStatusChanged(value));
    }

    // ------------------------------------------------------------------
    // Maintained vehicle state
    // ------------------------------------------------------------------

    /** The base component has no ACC model; it reports the OEM unknown ACC. */
    public int getAccStatus() {
        return -1;
    }

    public AirCondition getAirCondition() {
        return mState.airCondition();
    }

    public int getAirConditionState(AirConditionState state) {
        return mState.airConditionState(state);
    }

    public int getAlarmData() {
        return -1;
    }

    public int getAmbientTemperatureValue() {
        return mState.ambientTemperature();
    }

    public float getBatteryRemainingCapacity() {
        return mState.batteryRemainingCapacity();
    }

    public BatteryState getBatteryState() {
        return new BatteryState();
    }

    /** The OEM component never fills it: its decoder has no caller. */
    public int getBrakeFluidStatus() {
        return -1;
    }

    public BrakePadStatus getBrakePadStatus() {
        return null;
    }

    public CameraState getCameraState() {
        return new CameraState();
    }

    /** The emulator has no CAN box to report a version; the OEM component returns null. */
    public String getCanBoxVersion() {
        return null;
    }

    /** Raw CAN frames come from the CAN box; none is received in the emulator. */
    public int[] getCanRawData(int id) {
        return null;
    }

    public int getComprehensiveFuelConsumption() {
        return -1;
    }

    public float getComprehensiveFuelConsumptionEX() {
        return getFuelLevel().mAvgFuelConsumption;
    }

    public DVRState getDVRState() {
        return new DVRState();
    }

    public DoorStatus getDoorStatus() {
        return mState.doorStatus();
    }

    /** No consumption data reaches the component; the OEM base component returns null. */
    public EnergyConsumptionInfo getEnergyConsumptionInfo() {
        return null;
    }

    public EnergyConsumptionPercent getEnergyConsumptionPercent() {
        return null;
    }

    /** The OEM component never fills it: its decoder has no caller. */
    public int getEngineFluidStatus() {
        return -1;
    }

    /**
     * Only the generic CAN box message 0x15 fills it; the Dongfeng protocol has no engine speed.
     */
    public int getEngineSpeed() {
        return Integer.MIN_VALUE;
    }

    /** The OEM component never fills it: its decoder has no caller. */
    public int getEngineStatus() {
        return -1;
    }

    /** The OEM Dongfeng component never receives an engine temperature and reports 0. */
    public int getEngineTemperature() {
        return 0;
    }

    public RadarData getFrontRadarData() {
        return null;
    }

    public FuelLevel getFuelLevel() {
        return mState.fuelLevel();
    }

    public GearState getGearStatus() {
        return mState.gearStatus();
    }

    /** Only the generic CAN box message 0x10 fills it; the Dongfeng protocol has no hand brake. */
    public int getHandBrakeStatus() {
        return -1;
    }

    /** The OEM component reports HEV mode 0 and never forwards a change. */
    public int getHevSysMode() {
        return 0;
    }

    public int getIllumination() {
        return -1;
    }

    public int getInstantFuelConsumption() {
        return (int) getFuelLevel().mInstantaneousFuelConsumption;
    }

    /** The OEM IPK readiness service of A9 has no owned counterpart; the component returns none. */
    public IIpkSyncComponentBase getIpkSyncComponent() {
        return null;
    }

    public RadarData getLeftRadarData() {
        return null;
    }

    public LightStatus getLightStatus() {
        return mState.lightStatus();
    }

    public Odometer getOdometer() {
        return mState.odometer();
    }

    public PowerLevel getPowerLevel() {
        return new PowerLevel();
    }

    public int getRainfallLevel() {
        return 0;
    }

    /** Copies count values of a raw frame from offset, as the OEM component does. */
    public int[] getRawData(int[] data, int offset, int count) {
        int[] result = new int[count];

        for (int i = 0; i < count; i++) {
            result[i] = data[offset + i];
        }

        return result;
    }

    public int getReTracking() {
        return -1;
    }

    public RealityWarningInfo getRealityWarningInfo() {
        return null;
    }

    public RadarData getRearRadarData() {
        return null;
    }

    public RadarData getRightRadarData() {
        return null;
    }

    public SWCAngle getSWCAngle() {
        return null;
    }

    /** Only the generic CAN box message 0x33 fills it; the Dongfeng protocol has no seat belts. */
    public SeatBelt getSeatBeltStatus() {
        return null;
    }

    public long getSecondaryOdometer() {
        return -1;
    }

    /** Maps the system locale to the cluster language as the OEM component does. */
    public int getSystemLanguage() {
        Locale locale = Locale.getDefault();

        if (locale.equals(Locale.US) || locale.equals(Locale.ENGLISH) || locale.equals(Locale.UK)) {
            return LANG_EN;
        }

        return locale.equals(Locale.CHINA) ? LANG_CN : LANG_UNKNOWN;
    }

    public TPMSInfo getTPMSInfo() {
        return mState.tpmsInfo();
    }

    public int getVehicleCenterControlEnabled(VehicleCenterControlEnabled item) {
        return -1;
    }

    public VehicleIO getVehicleIO() {
        return null;
    }

    /** The OEM component never fills it: its decoder has no caller. */
    public int getVehicleKeyState() {
        return -1;
    }

    public int getVehicleSceneMode() {
        return 0;
    }

    public int getVehicleSpeed() {
        return mState.vehicleSpeed();
    }

    public int getVehicleState(VehicleState state) {
        return mState.vehicleState(state);
    }

    public WheelCount getWheelCountValue() {
        return null;
    }

    public WheelSpeed getWheelSpeed() {
        return null;
    }

    public WindowStatus getWindowStatus() {
        return new WindowStatus();
    }

    public int getWiperFluidStatus() {
        return -1;
    }

    /** The CAN box reports its functions; without one the OEM component supports none. */
    public boolean isSupportFunction(FunctionType function) {
        return false;
    }

    /** No CAN box is present to query. */
    public void queryVehicleState() {}

    /** The request goes to the CAN box; without one the OEM component reports -1. */
    public int requestTravellingInfo(TravellingInfoType type) {
        return -1;
    }

    public int requestVehicleCenterControlEnabledInfo() {
        return -1;
    }

    public int setAirConditionBundleState(Bundle bundle) {
        return mState.setAirConditionBundleState(bundle);
    }

    public void setAirConditionState(AirConditionState state, int value) {
        mState.setAirConditionState(state, value);
    }

    public void setCanRawData(int id, int[] data) {}

    public void setFactoryModeState(VehicleStateAutoTest state, String name, int value) {}

    public void setGearBoxType(GearBoxType type) {}

    public int setVehicleAndAirConditionBundleState(Bundle air, Bundle vehicle) {
        // OEM callers use null for the block they are not changing.
        if (air != null) {
            setAirConditionBundleState(air);
        }

        return vehicle == null ? 1 : setVehicleBundleState(vehicle);
    }

    public int setVehicleBundleState(Bundle bundle) {
        for (VehicleState state : VehicleState.values()) {
            if (bundle.containsKey(state.name())) {
                setVehicleState(state, bundle.getInt(state.name()));
            }
        }

        return 1;
    }

    public boolean setVehicleCenterControlEnabled(VehicleCenterControlEnabled item, int value) {
        return false;
    }

    public int setVehicleSceneMode(int mode) {
        return 0;
    }

    public void setVehicleState(VehicleState state, int value) {
        mState.setVehicleState(state, value);
    }

    // ------------------------------------------------------------------
    // Head-unit information for the cluster
    // ------------------------------------------------------------------

    public void sendIPKNavideath() {
        Log.d(TAG, "basecanbus sendIPKNavideath");
    }

    /** The OEM component sends no IVC frame for the call list. */
    public void updateCallListInfo(Bundle info) {}

    public void updateMediaPlayInfo(MediaPlayInfo info) {}

    public void updateMediaPlayMode(MediaPlayMode mode) {}

    public void updateMediaPlayProgressInfo(MediaPlayProgressInfo info) {}

    public void updateMediaSeqType(int type) {}

    /** Sends the song and artist or the radio station of the current source to the cluster. */
    public void updateMediaSrcInfo(MediaSrcInfo info) {
        if (info == null) {
            return;
        }

        try {
            mState.ipk()
                    .media(
                            "canbus",
                            name(info.getSrcType()),
                            name(info.getMediaState()),
                            info.getFrequency(),
                            info.getSongName(),
                            info.getArtist(),
                            info.getAlbumName());
        } catch (IllegalStateException rejected) {
            Log.w(TAG, "cluster media rejected: " + rejected.getMessage());
        }
    }

    public void updateMediaVolume(int volume) {}

    /** Sends the turn-by-turn guidance or the roaming road name to the cluster. */
    public void updateNaviInfo(NaviInfo info) {
        if (info == null) {
            return;
        }

        try {
            mState.ipk()
                    .navi(
                            "canbus",
                            name(info.getGuideState()),
                            info.getTbtIconId(),
                            info.getNextDistance(),
                            info.getTurnRoadName(),
                            info.getCurrentRoadName());
        } catch (IllegalStateException rejected) {
            Log.w(TAG, "cluster navigation rejected: " + rejected.getMessage());
        }
    }

    /** Sends the lane ids of the bundle values, or hides the lanes. */
    public void updateNaviLaneInfo(boolean shown, Bundle info) {
        try {
            mState.ipk().lanes("canbus", shown, IpkInfoSync.laneIds(shown ? info : null));
        } catch (IllegalStateException rejected) {
            Log.w(TAG, "cluster lanes rejected: " + rejected.getMessage());
        }
    }

    /** Publishes PhoneInfo to the cluster over the IPK link. */
    public void updatePhoneInfo(PhoneInfo info) {
        mState.publishPhone(info);
    }

    private static String name(Enum<?> value) {
        return value == null ? null : value.name();
    }

    // ------------------------------------------------------------------
    // CAN box transport
    // ------------------------------------------------------------------

    /** Raw frames of the CAN box have no decoder in the emulator component. */
    public boolean dispatchCanData(int id, int[] data) {
        return false;
    }

    /** Copies count values of a socket frame from offset, as the OEM component does. */
    public int[] makeSockRawData(int[] data, int offset, int count) {
        int[] result = new int[count];

        for (int i = 0; i < count && offset + i < data.length; i++) {
            result[i] = data[offset + i];
        }

        return result;
    }

    /** Inbound raw MCU frame; without a CAN box decoder it is not dispatched. */
    public boolean notify(int id, int[] data) {
        if (data == null) {
            Log.w(TAG, "notify data is null");

            return false;
        }

        return dispatchCanData(id, data);
    }

    /** Inbound raw MCU frame bundle; the emulator receives no CAN box frames. */
    public boolean notify(int id, Bundle data) {
        Log.w(TAG, "notify bundle frame without CAN box: " + id);

        return false;
    }

    /** Inbound raw socket frame; without a CAN box decoder it is not dispatched. */
    public boolean socketNotify(int id, int[] data) {
        if (data == null) {
            Log.w(TAG, "notify data is null");

            return false;
        }

        return false;
    }

    public void registerOperator(ICanBusOperator operator, int canFd) {
        mCanFd = canFd;

        if (mOperator == null) {
            mOperator = operator;
        }
    }

    public void registerOperator(ICanBusOperator operator, int canFd, int muFd) {
        mCanFd = canFd;
        mMuFd = muFd;

        if (mOperator == null) {
            mOperator = operator;
        }
    }

    public int sendCanIOCtl(int command, byte[] data) {
        return sendCanIOCtl(command, data, true);
    }

    /** Sends a CAN box command through the registered operator; -1 without one, as the OEM. */
    public int sendCanIOCtl(int command, byte[] data, boolean log) {
        ICanBusOperator operator = mOperator;

        if (log && data != null) {
            Log.d(TAG, "SysToCanBox cmd " + command + " data:" + hex(data, 0, data.length));
        }

        if (operator == null) {
            Log.e(TAG, "mCanBusOperator is null");

            return -1;
        }

        return operator.sendCanCmd(mCanFd, command, data);
    }

    public byte[] sendCanIOCtlBytes(int command, byte[] data) {
        ICanBusOperator operator = mOperator;

        if (operator == null) {
            Log.e(TAG, "mCanBusOperator is null");

            return null;
        }

        return operator.sendCanCmdBytes(mCanFd, command, data);
    }

    public int sendMuIoCtl(int command, byte[] data) {
        ICanBusOperator operator = mOperator;

        if (operator == null) {
            Log.e(TAG, "mCanBusOperator is null");

            return -1;
        }

        return operator.sendMuCmd(mMuFd, command, data);
    }

    /**
     * Without a registered operator, command 0 writes one complete IVC frame over the IPK link, as
     * the OEM JNI socket does.
     */
    public int sendSocketCtl(int command, byte[] data) {
        ICanBusOperator operator = mOperator;

        if (operator == null && command == 0 && IpkFrames.command(data) >= 0) {
            try {
                return mState.ipk().raw("canbus", data) > 0 ? 0 : -1;
            } catch (IllegalStateException rejected) {
                Log.w(TAG, "cluster frame rejected: " + rejected.getMessage());

                return -1;
            }
        }

        if (operator == null) {
            Log.e(TAG, "mCanBusOperator is null");

            return -1;
        }

        return operator.sendSocketCmd(command, data);
    }

    public void printData(String trace, byte[] data) {
        if (data != null && data.length > 0) {
            Log.d(TAG, trace + ", len:" + data.length + " " + hex(data, 0, data.length));
        }
    }

    public void printData(String trace, int length, int[] data) {
        if (data != null && length > 0) {
            StringBuilder text = new StringBuilder();

            for (int i = 0; i < length && i < data.length; i++) {
                text.append(String.format(Locale.ROOT, "%02x ", data[i]));
            }

            Log.d(TAG, trace + ", len:" + length + " [" + text + "]");
        }
    }

    public void printDataWithLog(String trace, byte[] data) {
        if (data != null) {
            Log.i(TAG, trace + ", len:" + data.length + " " + hex(data, 0, data.length));
        }
    }

    private static String hex(byte[] data, int offset, int length) {
        StringBuilder text = new StringBuilder("[");

        for (int i = offset; i < offset + length; i++) {
            text.append(String.format(Locale.ROOT, "%02x ", data[i]));
        }

        return text.append(']').toString();
    }
}
