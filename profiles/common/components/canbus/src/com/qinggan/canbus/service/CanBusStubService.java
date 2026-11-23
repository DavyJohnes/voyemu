package com.qinggan.canbus.service;

import android.content.Context;
import android.os.Bundle;

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
import com.qinggan.canbus.ICanBusService;
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
import com.qinggan.ipksync.IIpkSyncService;

/**
 * Emulator-side "qg.canbus" binder service (com.qinggan.canbus.ICanBusService, descriptor
 * identical, transaction codes from the OEM donor DEX declared by the profile). As in the OEM
 * service, every vehicle call goes to the CAN bus component, which keeps the observers and the
 * vehicle state; this service adds the registration status and the observation journal reads.
 */
public class CanBusStubService extends ICanBusService.Stub {
    private final BaseCanBusComponent mComponent;
    private final CanBusState mState;
    private final IpkSyncBinder mIpkSync;
    private String mRegistration = "pending";

    public CanBusStubService(Context context, BaseCanBusComponent component) {
        component.init(context);
        mComponent = component;
        mState = component.state();
        mIpkSync = new IpkSyncBinder(mState.ipk());
    }

    // ------------------------------------------------------------------
    // Lifecycle and observation
    // ------------------------------------------------------------------

    void attachModel() {
        mComponent.start();
    }

    void stopModelPolling() {
        mComponent.stop();
    }

    /** Requests head-unit power from the framework screen lifecycle. */
    public void setAccStatus(int value) {
        mComponent.setAccStatus(value);
    }

    int[] clusterThemeSnapshot() {
        return mState.clusterThemeSnapshot();
    }

    void registerClusterTheme() {
        mState.registerClusterTheme(this);
    }

    /** Closed IPK history request: schema 1, cursor and limit 1..256, by default 128. */
    private static int ipkLimit(org.json.JSONObject query) throws org.json.JSONException {
        java.util.Iterator<String> keys = query.keys();

        while (keys.hasNext()) {
            String key = keys.next();

            if (!"schema".equals(key) && !"cursor".equals(key) && !"limit".equals(key)) {
                throw new IllegalArgumentException("unknown IPK history argument");
            }
        }

        if (!query.has("schema") || !Integer.valueOf(1).equals(query.get("schema"))) {
            throw new IllegalArgumentException("IPK history schema 1 required");
        }

        int limit = query.has("limit") ? query.getInt("limit") : 128;

        if (limit < 1 || limit > 256) {
            throw new IllegalArgumentException("IPK history limit 1..256");
        }

        return limit;
    }

    private static org.json.JSONObject ipkCursor(org.json.JSONObject query)
            throws org.json.JSONException {
        org.json.JSONObject cursor = query.getJSONObject("cursor");

        if (cursor.length() != 2
                || !(cursor.get("source_instance") instanceof String)
                || !(cursor.get("after_seq") instanceof String)
                || !cursor.getString("after_seq").matches("0|[1-9][0-9]{0,18}")) {
            throw new IllegalArgumentException("invalid IPK history cursor");
        }

        return cursor;
    }

    /** Head-unit information for the cluster; its link is shared with the cluster service. */
    IpkInfoSync ipk() {
        return mState.ipk();
    }

    void startIpk() {
        mState.ipk().link().start();
    }

    /**
     * Sent and received IPK frames of this service: request {"schema":1} with an optional cursor
     * {"source_instance","after_seq"} and limit 1..256; the events carry no frame text.
     */
    org.json.JSONObject ipkHistory(String request) throws org.json.JSONException {
        org.json.JSONObject query = new org.json.JSONObject(request == null ? "{}" : request);
        int limit = ipkLimit(query);
        org.json.JSONObject cursor = query.has("cursor") ? ipkCursor(query) : null;
        IpkLink link = mState.ipk().link();
        String instance = mState.sourceInstance();
        long after = 0;
        String gap = null;
        org.json.JSONArray events = new org.json.JSONArray();
        long oldest, latest;

        // One window of the journal: the link appends under its own lock.
        synchronized (link) {
            oldest = link.oldest();
            latest = link.latest();

            if (cursor != null) {
                after = Long.parseLong(cursor.getString("after_seq"));

                if (!instance.equals(cursor.getString("source_instance"))) {
                    gap = "source_replaced";
                    after = 0;
                } else if (after > latest) {
                    gap = "cursor_ahead";
                    after = 0;
                } else if (after < oldest - 1) {
                    gap = "overflow";
                }
            }

            for (CanObservationJournal.Record record : link.history(after, limit)) {
                events.put(new org.json.JSONObject(record.json));
                after = record.seq;
            }
        }

        String bootId = mState.bootId();

        return new org.json.JSONObject()
                .put("schema", 1)
                .put("source", "android:qg.canbus:ipk")
                .put("status", "observed")
                .put("boot_id", bootId == null ? org.json.JSONObject.NULL : bootId)
                .put("source_instance", instance)
                .put("link", new org.json.JSONObject(link.status()))
                .put("oldest_seq", Long.toString(oldest))
                .put("latest_seq", Long.toString(latest))
                .put("gap", gap == null ? org.json.JSONObject.NULL : gap)
                .put("events", events)
                .put(
                        "next_cursor",
                        new org.json.JSONObject()
                                .put("source_instance", instance)
                                .put("after_seq", Long.toString(after)));
    }

    /** Mode commands the component accepted; see {@link ModeCommandHistory#history}. */
    org.json.JSONObject modeHistory(String request) throws org.json.JSONException {
        return mState.modeHistory().history(request);
    }

    PhoneDeliveryDiagnostics phoneDeliveryDiagnostics() {
        return mState.phoneDiagnostics();
    }

    VehicleInputSnapshot currentInputs() {
        return mState.inputs();
    }

    public boolean applyVehicleInputs(String encoded) {
        return mState.applyVehicleInputs(encoded);
    }

    public boolean applyVehicleInputs(String encoded, int acc) {
        return mState.applyVehicleInputs(encoded, acc);
    }

    public boolean applyVehicleSnapshot(
            long sequence, int speed, int soc, int doorFl, int left, int right) {
        return mState.applyVehicleSnapshot(sequence, speed, soc, doorFl, left, right);
    }

    org.json.JSONObject phoneDiagnostics() throws org.json.JSONException {
        return new org.json.JSONObject(mState.phoneDiagnostics().snapshot())
                .put("schema", 1)
                .put("source", "android:qg.canbus:phone")
                .put(
                        "boot_id",
                        mState.bootId() == null ? org.json.JSONObject.NULL : mState.bootId())
                .put("source_instance", mState.sourceInstance())
                .put("pid", android.os.Process.myPid());
    }

    void registrationObserved(boolean registered) {
        synchronized (mState.lock()) {
            mRegistration = registered ? "registered" : "failed";
            mState.record("registration", null, "transport.registration", registered ? 1 : 0, null);
        }
    }

    /** Reads the requested vehicle states; the caller holds the state lock. */
    private org.json.JSONObject observedValues(CanObservationRequest request)
            throws org.json.JSONException {
        org.json.JSONObject values = new org.json.JSONObject();

        for (VehicleState state : request.states) {
            int raw = mComponent.getVehicleState(state);
            values.put(
                    "vehicle_state." + state.name(),
                    new org.json.JSONObject()
                            .put(
                                    "status",
                                    CanModelBridge.isModelState(state) && raw < 0
                                            ? "unavailable"
                                            : "observed")
                            .put("raw", raw)
                            .put("basis", "ICanBusService.getVehicleState"));
        }

        return values;
    }

    org.json.JSONObject observe(CanObservationRequest request) throws org.json.JSONException {
        synchronized (mState.lock()) {
            CanObservationJournal journal = mState.journal();
            String instance = mState.sourceInstance();
            String bootId = mState.bootId();
            org.json.JSONObject values = observedValues(request);
            long latest = journal.latest();
            long after = request.instance == null ? latest : request.after;
            String gap = null;

            if (request.instance != null && !instance.equals(request.instance)) {
                gap = "source_replaced";
                after = 0;
            } else if (after > latest) {
                gap = "cursor_ahead";
                after = 0;
            }

            if (after < journal.oldest() - 1 && gap == null) {
                gap = "overflow";
            }

            org.json.JSONArray events = new org.json.JSONArray();

            for (CanObservationJournal.Record record : journal.after(after, request.limit)) {
                events.put(new org.json.JSONObject(record.json));
                after = record.seq;
            }

            org.json.JSONObject cursor =
                    new org.json.JSONObject()
                            .put("source_instance", instance)
                            .put("after_seq", Long.toString(after));

            return new org.json.JSONObject()
                    .put("schema", 1)
                    .put("source", "android:qg.canbus")
                    .put(
                            "status",
                            bootId == null
                                    ? "unavailable"
                                    : "registered".equals(mRegistration) ? "ready" : "not-ready")
                    .put("boot_id", bootId == null ? org.json.JSONObject.NULL : bootId)
                    .put("source_instance", instance)
                    .put("pid", android.os.Process.myPid())
                    .put("service_started_elapsed_ns", Long.toString(mState.startedNs()))
                    .put("revision", Long.toString(latest))
                    .put(
                            "clock",
                            new org.json.JSONObject()
                                    .put("domain", "android.elapsedRealtimeNanos")
                                    .put(
                                            "sampled_ns",
                                            Long.toString(
                                                    android.os.SystemClock.elapsedRealtimeNanos())))
                    .put("values", values)
                    .put(
                            "acc",
                            new org.json.JSONObject()
                                    .put("status", "observed")
                                    .put("raw", mComponent.getAccStatus())
                                    .put("basis", "ICanBusService.getAccStatus"))
                    .put(
                            "transport",
                            new org.json.JSONObject()
                                    .put("registration", mRegistration)
                                    .put("binder_alive", isBinderAlive())
                                    .put("client_manager_cache", "not_observed"))
                    .put(
                            "coverage",
                            new org.json.JSONObject()
                                    .put("begins", "service_construction")
                                    .put("delivery", "journal_replay")
                                    .put("vehicle_state_setter", true)
                                    .put("other_feedback_paths", "partial")
                                    .put("survives_process_death", false))
                    .put(
                            "history",
                            new org.json.JSONObject()
                                    .put("oldest_seq", Long.toString(journal.oldest()))
                                    .put("latest_seq", Long.toString(latest))
                                    .put("dropped_count", Long.toString(journal.dropped()))
                                    .put("next_cursor", cursor)
                                    .put("has_more", after < latest)
                                    .put("gap", gap == null ? org.json.JSONObject.NULL : gap))
                    .put("events", events);
        }
    }

    // ------------------------------------------------------------------
    // ICanBusService
    // ------------------------------------------------------------------

    @Override
    public boolean addCallback(ICanBusServiceCallback cb) {
        return mComponent.attachObserver(cb);
    }

    @Override
    public boolean removeCallback(ICanBusServiceCallback cb) {
        return mComponent.detachObserver(cb);
    }

    @Override
    public Odometer getOdometer() {
        return mComponent.getOdometer();
    }

    @Override
    public DoorStatus getDoorStatus() {
        return mComponent.getDoorStatus();
    }

    @Override
    public DVRState getDVRState() {
        return mComponent.getDVRState();
    }

    @Override
    public String getCanBoxVersion() {
        return mComponent.getCanBoxVersion();
    }

    @Override
    public LightStatus getLightStatus() {
        return mComponent.getLightStatus();
    }

    @Override
    public GearState getGearStatus() {
        return mComponent.getGearStatus();
    }

    @Override
    public int getAmbientTemperature() {
        return mComponent.getAmbientTemperatureValue();
    }

    @Override
    public EnergyConsumptionInfo getEnergyConsumptionInfo() {
        return mComponent.getEnergyConsumptionInfo();
    }

    @Override
    public EnergyConsumptionPercent getEnergyConsumptionPercent() {
        return mComponent.getEnergyConsumptionPercent();
    }

    @Override
    public WheelSpeed getWheelSpeed() {
        return mComponent.getWheelSpeed();
    }

    @Override
    public FuelLevel getFuelLevel() {
        return mComponent.getFuelLevel();
    }

    @Override
    public WheelCount getWheelCount() {
        return mComponent.getWheelCountValue();
    }

    @Override
    public int getEngineTemperature() {
        return mComponent.getEngineTemperature();
    }

    @Override
    public BrakePadStatus getBrakePadStatus() {
        return mComponent.getBrakePadStatus();
    }

    @Override
    public int getBrakeFluidStatus() {
        return mComponent.getBrakeFluidStatus();
    }

    @Override
    public int getWiperFluidStatus() {
        return mComponent.getWiperFluidStatus();
    }

    @Override
    public int getInstantFuelConsumption() {
        return mComponent.getInstantFuelConsumption();
    }

    @Override
    public int getComprehensiveFuelConsumption() {
        return mComponent.getComprehensiveFuelConsumption();
    }

    @Override
    public float getComprehensiveFuelConsumptionEX() {
        return mComponent.getComprehensiveFuelConsumptionEX();
    }

    @Override
    public int getEngineStatus() {
        return mComponent.getEngineStatus();
    }

    @Override
    public BatteryState getBatteryState() {
        return mComponent.getBatteryState();
    }

    @Override
    public void queryVehicleState() {
        mComponent.queryVehicleState();
    }

    @Override
    public SeatBelt getSeatBeltStatus() {
        return mComponent.getSeatBeltStatus();
    }

    @Override
    public int getIllumination() {
        return mComponent.getIllumination();
    }

    @Override
    public VehicleIO getVehicleIO() {
        return mComponent.getVehicleIO();
    }

    @Override
    public long getSecondaryOdometer() {
        return mComponent.getSecondaryOdometer();
    }

    @Override
    public int getVehicleKeyState() {
        return mComponent.getVehicleKeyState();
    }

    @Override
    public int getVehicleSpeed() {
        return mComponent.getVehicleSpeed();
    }

    @Override
    public int getEngineSpeed() {
        return mComponent.getEngineSpeed();
    }

    @Override
    public AirCondition getAirCondition() {
        return mComponent.getAirCondition();
    }

    @Override
    public RadarData getFrontRadarData() {
        return mComponent.getFrontRadarData();
    }

    @Override
    public RadarData getRearRadarData() {
        return mComponent.getRearRadarData();
    }

    @Override
    public RadarData getLeftRadarData() {
        return mComponent.getLeftRadarData();
    }

    @Override
    public RadarData getRightRadarData() {
        return mComponent.getRightRadarData();
    }

    @Override
    public int getReTracking() {
        return mComponent.getReTracking();
    }

    @Override
    public int getHandBrakeStatus() {
        return mComponent.getHandBrakeStatus();
    }

    @Override
    public int getEngineFluidStatus() {
        return mComponent.getEngineFluidStatus();
    }

    @Override
    public int getAlarmData() {
        return mComponent.getAlarmData();
    }

    @Override
    public SWCAngle getSWCAngle() {
        return mComponent.getSWCAngle();
    }

    @Override
    public int getHevSysMode() {
        return mComponent.getHevSysMode();
    }

    @Override
    public WindowStatus getWindowStatus() {
        return mComponent.getWindowStatus();
    }

    /** Like the OEM service, reports the ACC of the component: CAN ACC OFF=0, ON=2, unknown -1. */
    @Override
    public int getAccStatus() {
        return mComponent.getAccStatus();
    }

    /** The IPK sync service writes to the cluster over the one IPK link of the guest. */
    @Override
    public IIpkSyncService getIpkSyncSvr() {
        return mIpkSync;
    }

    @Override
    public TPMSInfo getTPMSInfo() {
        return mComponent.getTPMSInfo();
    }

    @Override
    public float getBatteryRemainingCapacity() {
        return mComponent.getBatteryRemainingCapacity();
    }

    @Override
    public PowerLevel getPowerlLevel() {
        return mComponent.getPowerLevel();
    }

    @Override
    public int getRainfallLevel() {
        return mComponent.getRainfallLevel();
    }

    @Override
    public int getVehicleSceneMode() {
        return mComponent.getVehicleSceneMode();
    }

    @Override
    public RealityWarningInfo getRealityWarningInfo() {
        return mComponent.getRealityWarningInfo();
    }

    @Override
    public CameraState getCameraState() {
        return mComponent.getCameraState();
    }

    @Override
    public int getVehicleState(VehicleState state) {
        return mComponent.getVehicleState(state);
    }

    @Override
    public int getAirConditionState(AirConditionState state) {
        return mComponent.getAirConditionState(state);
    }

    @Override
    public int getVehicleCenterControlEnabled(VehicleCenterControlEnabled item) {
        return mComponent.getVehicleCenterControlEnabled(item);
    }

    @Override
    public void setAirConditionState(AirConditionState state, int value) {
        mComponent.setAirConditionState(state, value);
    }

    @Override
    public int setAirConditionBundleState(Bundle bundle) {
        return mComponent.setAirConditionBundleState(bundle);
    }

    @Override
    public int setVehicleBundleState(Bundle bundle) {
        return mComponent.setVehicleBundleState(bundle);
    }

    @Override
    public int setVehicleAndAirConditionBundleState(Bundle air, Bundle vehicle) {
        return mComponent.setVehicleAndAirConditionBundleState(air, vehicle);
    }

    @Override
    public void setVehicleState(VehicleState state, int value) {
        mComponent.setVehicleState(state, value);
    }

    @Override
    public boolean setVehicleCenterControlEnabled(VehicleCenterControlEnabled item, int value) {
        return mComponent.setVehicleCenterControlEnabled(item, value);
    }

    @Override
    public int requestVehicleCenterControlEnabledInfo() {
        return mComponent.requestVehicleCenterControlEnabledInfo();
    }

    @Override
    public int requestTravellingInfo(TravellingInfoType type) {
        return mComponent.requestTravellingInfo(type);
    }

    @Override
    public boolean isSupportFunction(FunctionType function) {
        return mComponent.isSupportFunction(function);
    }

    @Override
    public void setGearBoxType(GearBoxType type) {
        mComponent.setGearBoxType(type);
    }

    @Override
    public void setFactoryModeState(VehicleStateAutoTest state, String name, int value) {
        mComponent.setFactoryModeState(state, name, value);
    }

    /** Factory test results go to the factory test station; the emulator has none. */
    @Override
    public void setFactoryModeResult(int result, String name) {}

    @Override
    public int setVehicleSceneMode(int mode) {
        return mComponent.setVehicleSceneMode(mode);
    }

    @Override
    public void updateMediaSrcInfo(MediaSrcInfo info) {
        mComponent.updateMediaSrcInfo(info);
    }

    @Override
    public void updateMediaPlayInfo(MediaPlayInfo info) {
        mComponent.updateMediaPlayInfo(info);
    }

    @Override
    public void updateMediaPlayProgressInfo(MediaPlayProgressInfo info) {
        mComponent.updateMediaPlayProgressInfo(info);
    }

    @Override
    public void updateMediaSeqType(int type) {
        mComponent.updateMediaSeqType(type);
    }

    @Override
    public void updateMediaPlayMode(MediaPlayMode mode) {
        mComponent.updateMediaPlayMode(mode);
    }

    @Override
    public void updateMediaVolume(int volume) {
        mComponent.updateMediaVolume(volume);
    }

    @Override
    public void updateNaviInfo(NaviInfo info) {
        mComponent.updateNaviInfo(info);
    }

    @Override
    public void updateNaviLaneInfo(boolean shown, Bundle info) {
        mComponent.updateNaviLaneInfo(shown, info);
    }

    @Override
    public void updatePhoneInfo(PhoneInfo info) {
        mComponent.updatePhoneInfo(info);
    }

    @Override
    public void updateCallListInfo(Bundle info) {
        mComponent.updateCallListInfo(info);
    }

    @Override
    public void setCanRawData(int id, int[] data) {
        mComponent.setCanRawData(id, data);
    }

    @Override
    public int[] getCanRawData(int id) {
        return mComponent.getCanRawData(id);
    }
}
