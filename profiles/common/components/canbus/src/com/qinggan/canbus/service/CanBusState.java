package com.qinggan.canbus.service;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;

import com.qinggan.canbus.AirCondition;
import com.qinggan.canbus.AirConditionState;
import com.qinggan.canbus.DoorStatus;
import com.qinggan.canbus.FuelLevel;
import com.qinggan.canbus.GearState;
import com.qinggan.canbus.ICanBusServiceCallback;
import com.qinggan.canbus.LightStatus;
import com.qinggan.canbus.Odometer;
import com.qinggan.canbus.PhoneInfo;
import com.qinggan.canbus.TPMSInfo;
import com.qinggan.canbus.VehicleState;

import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Vehicle state the CAN bus component maintains: the persisted complete vehicle input, the legacy
 * vehicle snapshot, the settings written through the component, the cluster theme, the emulator
 * climate and the ACC request of the screen lifecycle. It journals the observed writes and
 * deliveries and owns the head-unit information for the cluster. Observers learn about a vehicle
 * input only when the values behind a callback change.
 */
public final class CanBusState {
    private static final String TAG = "CanBusStub";
    private static final int SETTING_FEEDBACK_MS = 200;
    private static final int INITIAL_PUSH_MS = 300;
    private final BaseCanBusComponent mComponent;
    private final Object mLock = new Object();
    private final Handler mHandler;
    private final SharedPreferences mClimate;
    private final VehicleInputStore mVehicleInputStore;
    private final ClusterTheme mTheme;
    private final CanObservationJournal mJournal = new CanObservationJournal(1024);
    private final String mSourceInstance = java.util.UUID.randomUUID().toString();
    private final String mBootId = readBootId();
    private final long mStartedNs = android.os.SystemClock.elapsedRealtimeNanos();
    private final PhoneDeliveryDiagnostics mPhoneDiagnostics = new PhoneDeliveryDiagnostics();
    private final IpkInfoSync mIpk = new IpkInfoSync(new IpkLink(IpkLink.IVCD));
    private final ModeCommandHistory mModeHistory;

    /** Last value set per state; Setting reads back IPK_THEM_MODE etc. */
    private final Map<VehicleState, Integer> mVehicleStates = new HashMap<>();

    /** Sequence 0: no legacy snapshot yet; the OEM initial values apply. */
    private volatile VehicleSnapshot mVehicleSnapshot = new VehicleSnapshot(0, -1, -1, -1, -1, -1);

    private volatile VehicleInputSnapshot mVehicleInputs;
    private long mVehicleQueuedSequence;
    private long mWriteSequence;
    private volatile String mDeliveringWrite;

    /**
     * Last ACC requested from the framework screen lifecycle; the service starts with the screen
     * on, so ON is requested only after an OFF. getAccStatus never reports it.
     */
    private int mAccRequest = 2;

    private static final class VehicleSnapshot {
        final long sequence;
        final int speed, soc, doorFl, left, right;

        VehicleSnapshot(long sequence, int speed, int soc, int doorFl, int left, int right) {
            this.sequence = sequence;
            this.speed = speed;
            this.soc = soc;
            this.doorFl = doorFl;
            this.left = left;
            this.right = right;
        }
    }

    /** Values behind one observer callback; a callback fires when they change. */
    private enum Signal {
        SPEED,
        SOC,
        DOOR,
        LIGHT,
        GEAR,
        AMBIENT,
        ODOMETER,
        TPMS,
        FUEL,
        TJA
    }

    CanBusState(BaseCanBusComponent component, Context context) {
        mComponent = component;
        record("service_started", null, null, null, null);
        mClimate = context.getSharedPreferences("voyemu-climate", Context.MODE_PRIVATE);
        mModeHistory =
                new ModeCommandHistory(
                        context.getSharedPreferences("voyemu-mode-history", Context.MODE_PRIVATE),
                        mBootId,
                        mSourceInstance);
        mVehicleInputStore = new VehicleInputStore(context);
        mVehicleInputs = mVehicleInputStore.load();

        if (mVehicleInputs != null) {
            mVehicleQueuedSequence = mVehicleInputs.sequence;
        }

        HandlerThread thread = new HandlerThread("canbus-stub");
        thread.start();
        mHandler = new Handler(thread.getLooper());
        mTheme = new ClusterTheme(context);
    }

    public Handler handler() {
        return mHandler;
    }

    // ------------------------------------------------------------------
    // Journal
    // ------------------------------------------------------------------

    private static String readBootId() {
        try (java.io.BufferedReader reader =
                new java.io.BufferedReader(
                        new java.io.FileReader("/proc/sys/kernel/random/boot_id"))) {
            String value = reader.readLine();

            return value != null && value.matches("[0-9a-fA-F-]{36}") ? value : null;
        } catch (java.io.IOException failure) {
            return null;
        }
    }

    /** Caller or callback identity of one journal event; null fields stay out of the event. */
    private static final class JournalSource {
        final Integer uid, pid;
        final String method;
        final IBinder callback;

        JournalSource(Integer uid, Integer pid, String method, IBinder callback) {
            this.uid = uid;
            this.pid = pid;
            this.method = method;
            this.callback = callback;
        }

        void put(org.json.JSONObject event) throws org.json.JSONException {
            if (uid != null) {
                event.put("caller_uid", uid)
                        .put("caller_pid", pid)
                        .put("initiator_product", "unknown");
            }

            if (method != null) {
                event.put("method", method);
            }

            if (callback != null) {
                event.put("callback", Integer.toHexString(System.identityHashCode(callback)));
            }
        }
    }

    void record(String kind, String write, String field, Integer raw, Integer previous) {
        record(kind, write, field, raw, previous, new JournalSource(null, null, null, null));
    }

    /** Journals a write received through the component with the Binder caller identity. */
    public String recordWrite(VehicleState state, int value) {
        synchronized (mLock) {
            String write = Long.toString(++mWriteSequence);
            record(
                    "write_received",
                    write,
                    "vehicle_state." + state.name(),
                    value,
                    null,
                    new JournalSource(
                            android.os.Binder.getCallingUid(),
                            android.os.Binder.getCallingPid(),
                            null,
                            null));

            return write;
        }
    }

    void recordCallback(
            String kind, String write, String field, Integer raw, String method, IBinder callback) {
        record(kind, write, field, raw, null, new JournalSource(null, null, method, callback));
    }

    private void record(
            String kind,
            String write,
            String field,
            Integer raw,
            Integer previous,
            JournalSource source) {
        synchronized (mLock) {
            try {
                org.json.JSONObject event =
                        new org.json.JSONObject()
                                .put("seq", Long.toString(mJournal.next()))
                                .put("kind", kind)
                                .put("source_instance", mSourceInstance)
                                .put(
                                        "elapsed_ns",
                                        Long.toString(
                                                android.os.SystemClock.elapsedRealtimeNanos()))
                                .put("utc_ms", Long.toString(System.currentTimeMillis()));

                if (write != null) {
                    event.put("write_id", write);
                }

                if (field != null) {
                    event.put("field", field);
                }

                if (raw != null) {
                    event.put("raw", raw);
                }

                if (previous != null) {
                    event.put("previous", previous).put("unchanged", previous.equals(raw));
                }

                source.put(event);
                mJournal.append(event.toString());
            } catch (org.json.JSONException failure) {
                throw new IllegalStateException(failure);
            }
        }
    }

    /** Mode commands the component accepted. */
    public ModeCommandHistory modeHistory() {
        return mModeHistory;
    }

    /** The journal and its identity; callers hold lock(). */
    CanObservationJournal journal() {
        return mJournal;
    }

    Object lock() {
        return mLock;
    }

    String sourceInstance() {
        return mSourceInstance;
    }

    String bootId() {
        return mBootId;
    }

    long startedNs() {
        return mStartedNs;
    }

    /** The write answered by the delivery in progress on the component handler, or null. */
    String deliveringWrite() {
        return mDeliveringWrite;
    }

    // ------------------------------------------------------------------
    // Observers
    // ------------------------------------------------------------------

    /** Sends a new observer the current state, as the OEM service does after registration. */
    void observerAttached(final ICanBusServiceCallback observer) {
        mHandler.postDelayed(() -> pushInitialData(observer), INITIAL_PUSH_MS);
    }

    private void pushInitialData(ICanBusServiceCallback cb) {
        BaseCanBusComponent component = mComponent;

        if (!component.isObserver(cb)) {
            return;
        }

        try {
            int[] theme = mTheme.snapshot();
            cb.onVehicleStateChanged(VehicleState.IPK_THEM_MODE, theme[0]);
            cb.onVehicleStateChanged(VehicleState.IPK_THEME_SKIN, theme[1]);
            int initialPw = component.getVehicleState(VehicleState.HUM_VSP_FUNCTION_SW);

            if (initialPw >= 0) {
                component.deliverJournaled(
                        cb,
                        "onVehicleStateChanged",
                        "vehicle_state." + VehicleState.HUM_VSP_FUNCTION_SW.name(),
                        initialPw,
                        o -> o.onVehicleStateChanged(VehicleState.HUM_VSP_FUNCTION_SW, initialPw));
            }

            cb.onLightStatusChanged(component.getLightStatus());
            cb.onOdometerChanged(component.getOdometer());
            cb.onBatteryStateChanged(component.getBatteryState());
            cb.onAirConditionChanged(component.getAirCondition());
            cb.onDoorStatusChanged(component.getDoorStatus());
            cb.onWindowsStatusChanged(component.getWindowStatus());
            cb.onVehicleSpeedChanged(component.getVehicleSpeed());
            cb.onBatteryRemainingCapacityChanged(component.getBatteryRemainingCapacity());

            if (mVehicleInputs != null) {
                cb.onGearStatusChanged(component.getGearStatus());
                cb.onAmbientTemperatureChanged(component.getAmbientTemperatureValue());
                cb.onTPMSInfoChange(component.getTPMSInfo());
                cb.onFuelLevelChanged(component.getFuelLevel());
                cb.onVehicleStateChanged(
                        VehicleState.TJA_HWA_STS,
                        component.getVehicleState(VehicleState.TJA_HWA_STS));
            }

            int initialAcc = component.getAccStatus();

            if (initialAcc >= 0) {
                component.deliverJournaled(
                        cb,
                        "onAccStateChanged",
                        "acc",
                        initialAcc,
                        o -> o.onAccStateChanged(initialAcc));
            }

            String version = component.getCanBoxVersion();

            if (version != null) {
                cb.onCanBoxVersionChange(version);
            }
        } catch (RemoteException e) {
            Log.w(TAG, "initial push failed (client died?)", e);
        }
    }

    // ------------------------------------------------------------------
    // Vehicle input
    // ------------------------------------------------------------------

    VehicleInputSnapshot inputs() {
        return mVehicleInputs;
    }

    boolean applyVehicleSnapshot(
            long sequence, int speed, int soc, int doorFl, int left, int right) {
        if (sequence < 1
                || sequence > 0xffffffffL
                || speed < 0
                || speed > 255
                || soc < 0
                || soc > 100
                || doorFl < 0
                || doorFl > 1
                || left < 0
                || left > 1
                || right < 0
                || right > 1) {
            return false;
        }

        VehicleInputSnapshot complete = mVehicleInputs;

        if (complete != null) {
            return queueVehicleInputs(
                    complete.withLegacy(sequence, speed, soc, doorFl, left, right), false);
        }

        final VehicleSnapshot snapshot =
                new VehicleSnapshot(sequence, speed, soc, doorFl, left, right);

        synchronized (mLock) {
            if (sequence <= mVehicleQueuedSequence) {
                return false;
            }

            mVehicleQueuedSequence = sequence;
            mHandler.post(() -> publish(() -> mVehicleSnapshot = snapshot));
        }

        return true;
    }

    boolean applyVehicleInputs(String encoded) {
        VehicleInputSnapshot snapshot;

        try {
            snapshot = VehicleInputStore.parse(encoded);
        } catch (org.json.JSONException
                | IllegalArgumentException
                | java.time.DateTimeException e) {
            Log.w(TAG, "complete vehicle input rejected", e);

            return false;
        }

        return queueVehicleInputs(snapshot, true);
    }

    private boolean queueVehicleInputs(
            final VehicleInputSnapshot snapshot, boolean allowIdempotent) {
        synchronized (mLock) {
            if (snapshot.sequence <= mVehicleQueuedSequence) {
                return allowIdempotent
                        && snapshot.sequence == mVehicleQueuedSequence
                        && snapshot.sameValues(mVehicleInputs);
            }

            mVehicleQueuedSequence = snapshot.sequence;
            mHandler.post(() -> {
                // Persist before publication. Failed writes leave the previous revision active.
                if (!mVehicleInputStore.save(snapshot)) {
                    synchronized (mLock) {
                        if (mVehicleQueuedSequence == snapshot.sequence) {
                            mVehicleQueuedSequence =
                                    mVehicleInputs == null
                                            ? mVehicleSnapshot.sequence
                                            : mVehicleInputs.sequence;
                        }
                    }

                    Log.e(TAG, "complete vehicle revision could not be saved");

                    return;
                }

                publish(() -> mVehicleInputs = snapshot);
                Log.i(TAG, "complete vehicle revision applied: " + snapshot.sequence);
            });
        }

        return true;
    }

    /** Applies a revision on the handler and reports each callback whose values changed. */
    private void publish(Runnable apply) {
        Map<Signal, int[]> before = new HashMap<>();

        for (Signal signal : Signal.values()) {
            before.put(signal, values(signal));
        }

        synchronized (mLock) {
            apply.run();
        }

        BaseCanBusComponent component = mComponent;

        for (Signal signal : Signal.values()) {
            if (!Arrays.equals(before.get(signal), values(signal))) {
                report(component, signal);
            }
        }
    }

    private static void report(BaseCanBusComponent component, Signal signal) {
        switch (signal) {
            case SPEED:
                component.onVehicleSpeedChanged(component.getVehicleSpeed());
                break;
            case SOC:
                component.onBatteryRemainingCapacityChanged(
                        component.getBatteryRemainingCapacity());
                break;
            case DOOR:
                component.onDoorStatusChanged(component.getDoorStatus());
                break;
            case LIGHT:
                component.onLightStatusChanged(component.getLightStatus());
                break;
            case GEAR:
                component.onGearStatusChanged(component.getGearStatus());
                break;
            case AMBIENT:
                component.onAmbientTemperatureChanged(component.getAmbientTemperatureValue());
                break;
            case ODOMETER:
                component.onOdometerChanged(component.getOdometer());
                break;
            case TPMS:
                component.onTPMSInfoChanged(component.getTPMSInfo());
                break;
            case FUEL:
                component.onFuelLevelChanged(component.getFuelLevel());
                break;
            default:
                component.onVehicleStateChanged(
                        VehicleState.TJA_HWA_STS,
                        component.getVehicleState(VehicleState.TJA_HWA_STS));
                break;
        }
    }

    /** The maintained values behind one callback; null while the callback has no source. */
    private int[] values(Signal signal) {
        VehicleInputSnapshot inputs = mVehicleInputs;
        VehicleSnapshot legacy = mVehicleSnapshot;

        if (inputs == null) {
            if (legacy.sequence == 0) {
                return null;
            }

            switch (signal) {
                case SPEED:
                    return new int[] {legacy.speed};
                case SOC:
                    return new int[] {legacy.soc};
                case DOOR:
                    return new int[] {legacy.doorFl};
                case LIGHT:
                    return new int[] {legacy.left, legacy.right};
                default:
                    return null;
            }
        }

        switch (signal) {
            case SPEED:
                return raw(inputs, "speed");
            case SOC:
                return raw(inputs, "soc");
            case DOOR:
                return raw(inputs, "hood", "tail_gate", "door_fl", "door_fr", "door_rl", "door_rr");
            case LIGHT:
                return raw(
                        inputs,
                        "turn_left",
                        "turn_right",
                        "pos_lamp",
                        "low_beam",
                        "high_beam",
                        "rear_fog",
                        "bcm_DRLStatus");
            case GEAR:
                return new int[] {inputs.gear()};
            case AMBIENT:
                return new int[] {inputs.ambientC()};
            case ODOMETER:
                return new int[] {
                    inputs.raw("odo"),
                    inputs.fuelRangeKm(),
                    inputs.evRangeKm(),
                    inputs.evRealRangeKm(),
                    inputs.evCycleRangeKm()
                };
            case TPMS:
                return tpms(inputs);
            case FUEL:
                return new int[] {fuelPercent(inputs)};
            default:
                return new int[] {inputs.tjaState()};
        }
    }

    private static int[] raw(VehicleInputSnapshot inputs, String... names) {
        int[] values = new int[names.length];

        for (int i = 0; i < names.length; i++) {
            values[i] = inputs.raw(names[i]);
        }

        return values;
    }

    private static int[] tpms(VehicleInputSnapshot inputs) {
        String[] corners = {"LF", "RF", "LR", "RR"};
        int[] values = new int[corners.length * 2];

        for (int i = 0; i < corners.length; i++) {
            values[i * 2] = Float.floatToIntBits(inputs.tyrePressure(corners[i]));
            values[i * 2 + 1] = inputs.tyreWarning(corners[i]);
        }

        return values;
    }

    /** The fuel percentage, or -1 without a valid one. */
    private static int fuelPercent(VehicleInputSnapshot inputs) {
        return inputs == null || inputs.raw("fuel_percent") > 100 ? -1 : inputs.raw("fuel_percent");
    }

    /** Vehicle input ranges; without input the OEM initial Odometer. */
    public Odometer odometer() {
        VehicleInputSnapshot source = mVehicleInputs;
        Odometer value = new Odometer();

        if (source == null) {
            return value;
        }

        value.mBeenTravelingMileage = source.raw("odo");
        value.setTotalTravelingMileage(source.raw("odo"));
        value.mCanTravelMileage = source.fuelRangeKm();
        value.mEVCruisingRange = source.evRangeKm();
        // Named incoming estimates remain independent; no SOC/consumption formula is inferred.
        value.mEVCruisingRang_real = source.evRealRangeKm();
        value.mCanTravelMileage_real = source.fuelRangeKm();
        value.mEVCruisingRange_wltc = source.evCycleRangeKm();

        return value;
    }

    public DoorStatus doorStatus() {
        VehicleInputSnapshot source = mVehicleInputs;
        DoorStatus door = new DoorStatus();

        if (source == null) {
            VehicleSnapshot legacy = mVehicleSnapshot;

            if (legacy.sequence != 0) {
                door.fLDoor = legacy.doorFl;
            }

            return door;
        }

        door.bonnetDoor = source.raw("hood");
        door.loadSpace = source.raw("tail_gate");
        door.fLDoor = source.raw("door_fl");
        door.fRDoor = source.raw("door_fr");
        door.rLDoor = source.raw("door_rl");
        door.rRDoor = source.raw("door_rr");

        return door; // Door locks have no recovered mapping; OEM UNKNOWN remains.
    }

    public LightStatus lightStatus() {
        VehicleInputSnapshot source = mVehicleInputs;
        VehicleSnapshot legacy = mVehicleSnapshot;
        LightStatus light = new LightStatus();

        if (source == null) {
            if (legacy.sequence != 0) {
                light.directionL = legacy.left;
                light.directionR = legacy.right;
            }

            return light;
        }

        light.directionL = source.raw("turn_left");
        light.directionR = source.raw("turn_right");
        light.positionLamp = source.raw("pos_lamp");
        light.dippedBeam = source.raw("low_beam");
        light.mainBeam = source.raw("high_beam");
        light.fogRear = source.raw("rear_fog");
        light.dayLight = source.raw("bcm_DRLStatus");

        return light;
    }

    public GearState gearStatus() {
        VehicleInputSnapshot source = mVehicleInputs;

        if (source == null) {
            return GearState.Unknown;
        }

        switch (source.gear()) {
            case 0:
                return GearState.Parking;
            case 1:
                return GearState.Reverse;
            case 2:
                return GearState.Neutral;
            case 3:
                return GearState.Drive;
            default:
                return GearState.Unknown;
        }
    }

    public int ambientTemperature() {
        VehicleInputSnapshot source = mVehicleInputs;

        return source == null ? Integer.MIN_VALUE : source.ambientC();
    }

    /** The fuel percentage of the vehicle input; the other fields keep their OEM initial values. */
    public FuelLevel fuelLevel() {
        FuelLevel value = new FuelLevel();
        int percent = fuelPercent(mVehicleInputs);

        if (percent >= 0) {
            value.mPercentage = percent;
        }

        return value;
    }

    public TPMSInfo tpmsInfo() {
        VehicleInputSnapshot source = mVehicleInputs;
        TPMSInfo value = new TPMSInfo();

        if (source == null) {
            return value;
        }

        value.mLeftFrontTirePressureValue = source.tyrePressure("LF");
        value.mRightFrontTirePressureValue = source.tyrePressure("RF");
        value.mLeftRearTirePressureValue = source.tyrePressure("LR");
        value.mRightRearTirePressureValue = source.tyrePressure("RR");
        value.mLeftFrontTireWarningStatus = source.tyreWarning("LF");
        value.mRightFrontTireWarningStatus = source.tyreWarning("RF");
        value.mLeftRearTireWarningStatus = source.tyreWarning("LR");
        value.mRightRearTireWarningStatus = source.tyreWarning("RR");

        // System/temperature status has no recovered SDK mapping; remain UNKNOWN.
        return value;
    }

    public int vehicleSpeed() {
        VehicleInputSnapshot s = mVehicleInputs;

        return s == null ? mVehicleSnapshot.speed : s.raw("speed");
    }

    public float batteryRemainingCapacity() {
        VehicleInputSnapshot s = mVehicleInputs;

        return s == null ? mVehicleSnapshot.soc : s.raw("soc");
    }

    // ------------------------------------------------------------------
    // Settings written through the component
    // ------------------------------------------------------------------

    /**
     * IVI_SOC_MODESET seeds the DrivePreference power-mode radio group: 1=Smart, 2=Electric,
     * 3=Fuel, 4=Save, 5=Forced EV (a separate switch). 0 leaves the group without a checked element
     * (the slider parks before the first item), so the REEV default is Electric (2) — matching the
     * machine. DRIVING_MODE_SET uses 1=Eco; 0 selects no driving mode. Keep CAN feedback within the
     * current session; OEM VehicleMemoryManager owns account restoration.
     */
    public int vehicleState(VehicleState state) {
        VehicleInputSnapshot inputs = mVehicleInputs;

        if (inputs != null && state == VehicleState.TJA_HWA_STS) {
            return inputs.tjaState();
        }

        synchronized (mLock) {
            if (state == VehicleState.IPK_THEM_MODE) {
                return mTheme.mode();
            }

            if (state == VehicleState.IPK_THEME_SKIN) {
                return mTheme.skin();
            }

            Integer v = mVehicleStates.get(state);
            int initial =
                    state == VehicleState.DRIVING_MODE_SET
                            ? 1
                            : state == VehicleState.IVI_SOC_MODESET
                                            || state == VehicleState.HUM_VSP_FUNCTION_SW
                                    ? 2
                                    : 0;

            return v == null ? initial : v;
        }
    }

    /** Caches the persisted last value of a setting, as the OEM component does at startup. */
    public int initVehicleState(VehicleState state, int value) {
        synchronized (mLock) {
            mVehicleStates.put(state, value);

            return value;
        }
    }

    /** Applies the drive mode packet and records the fields the vehicle state accepted. */
    public void applyDriveModePacket(Map<VehicleState, Integer> states, String origin) {
        synchronized (mLock) {
            Map<String, Integer> applied = new LinkedHashMap<>();

            for (Map.Entry<VehicleState, Integer> entry : states.entrySet()) {
                if (applyVehicleState(entry.getKey(), entry.getValue())) {
                    applied.put(entry.getKey().name(), entry.getValue());
                }
            }

            if (applied.containsKey(ModeCommandHistory.DRIVING_MODE_SET)) {
                mModeHistory.accepted(ModeCommandHistory.DRIVING_MODE_SET, applied, origin);
            }
        }
    }

    /** Records the energy mode after the CAN2 model has acknowledged admission. */
    public void recordModelEnergyMode(int value) {
        synchronized (mLock) {
            String command = ModeCommandHistory.IVI_SOC_MODESET;
            mModeHistory.accepted(
                    command, Collections.singletonMap(command, value), ModeCommandHistory.CLIENT);
        }
    }

    /** Adds the paired seat switch or level change; the caller holds the lock. */
    private void putSeatChange(Map<VehicleState, Integer> changed, VehicleState state, int value) {
        VehicleState toggle = seatSwitch(state);
        VehicleState level = seatLevel(state);

        if (toggle != null) {
            // Seat levels turn the hardware on; feedback 2 means enabled.
            changed.put(toggle, value == 0 ? 1 : 2);
        } else if (level != null && value == 1) {
            // The OEM switch sends the same pulse to enable and disable.
            boolean enabled = vehicleState(state) != 2;
            changed.put(state, enabled ? 2 : 1);
            changed.put(level, enabled ? 3 : 0);
        }
    }

    /**
     * Applies a setting write and answers it as the OEM CAN box does: the setting response, its Ex
     * form and then the new value of every field the write changed.
     */
    public void setVehicleState(final VehicleState state, final int value) {
        applyVehicleState(state, value);
    }

    private boolean applyVehicleState(final VehicleState state, final int value) {
        final Map<VehicleState, Integer> changed = new HashMap<>();
        final String write = recordWrite(state, value);

        synchronized (mLock) {
            Map<VehicleState, Integer> previous = new HashMap<>();

            if (isTheme(state)) {
                previous.put(VehicleState.IPK_THEM_MODE, mTheme.mode());
                previous.put(VehicleState.IPK_THEME_SKIN, mTheme.skin());

                if (!mTheme.write(state, value, changed)) {
                    return false;
                }
            } else {
                changed.put(state, value);
            }

            putSeatChange(changed, state, value);

            for (VehicleState field : changed.keySet()) {
                previous.putIfAbsent(field, vehicleState(field));
            }

            for (Map.Entry<VehicleState, Integer> update : changed.entrySet()) {
                if (!isTheme(update.getKey())) {
                    mVehicleStates.put(update.getKey(), update.getValue());
                }
            }

            for (VehicleState field : changed.keySet()) {
                record(
                        "state_applied",
                        write,
                        "vehicle_state." + field.name(),
                        vehicleState(field),
                        previous.get(field));
            }
        }

        if (isTheme(state)) {
            mTheme.changed();
        }

        // OEM service answers with onVehicleStateSettingResponse(Ex) and then broadcasts the new
        // value via onVehicleStateChanged; the settings UI (e.g. Display theme checkmark) only
        // updates on the latter.
        mHandler.postDelayed(
                () -> {
                    BaseCanBusComponent component = mComponent;
                    mDeliveringWrite = write;

                    try {
                        component.onVehicleStateSettingResponse(state, true);
                        component.onVehicleStateSettingResponseEx(state, 1);

                        for (Map.Entry<VehicleState, Integer> update : changed.entrySet()) {
                            VehicleState field = update.getKey();
                            component.onVehicleStateChanged(
                                    field,
                                    isTheme(field) ? vehicleState(field) : update.getValue());
                        }
                    } finally {
                        mDeliveringWrite = null;
                    }
                },
                SETTING_FEEDBACK_MS);

        return true;
    }

    private static boolean isTheme(VehicleState state) {
        return state == VehicleState.IPK_THEM_MODE || state == VehicleState.IPK_THEME_SKIN;
    }

    private static VehicleState seatSwitch(VehicleState state) {
        if (state == VehicleState.FRONT_SEAT_HEATING_COMMAND_LEFT) {
            return VehicleState.FRONT_SEAT_HEATING_SWITCH_LEFT;
        }

        if (state == VehicleState.FRONT_SEAT_HEATING_COMMAND_RIGHT) {
            return VehicleState.FRONT_SEAT_HEATING_SWITCH_RIGHT;
        }

        if (state == VehicleState.FRONT_SEAT_VENTILATION_COMMAND_LEFT) {
            return VehicleState.FRONT_SEAT_VENTILATION_SWITCH_LEFT;
        }

        if (state == VehicleState.FRONT_SEAT_VENTILATION_COMMAND_RIGHT) {
            return VehicleState.FRONT_SEAT_VENTILATION_SWITCH_RIGHT;
        }

        if (state == VehicleState.FRONT_SEAT_MASS_INTEN_LEFT) {
            return VehicleState.FRONT_SEAT_MASS_SWITCH_LEFT;
        }

        if (state == VehicleState.FRONT_SEAT_MASS_INTEN_RIGHT) {
            return VehicleState.FRONT_SEAT_MASS_SWITCH_RIGHT;
        }

        return null;
    }

    private static VehicleState seatLevel(VehicleState state) {
        if (state == VehicleState.FRONT_SEAT_HEATING_SWITCH_LEFT) {
            return VehicleState.FRONT_SEAT_HEATING_COMMAND_LEFT;
        }

        if (state == VehicleState.FRONT_SEAT_HEATING_SWITCH_RIGHT) {
            return VehicleState.FRONT_SEAT_HEATING_COMMAND_RIGHT;
        }

        if (state == VehicleState.FRONT_SEAT_VENTILATION_SWITCH_LEFT) {
            return VehicleState.FRONT_SEAT_VENTILATION_COMMAND_LEFT;
        }

        if (state == VehicleState.FRONT_SEAT_VENTILATION_SWITCH_RIGHT) {
            return VehicleState.FRONT_SEAT_VENTILATION_COMMAND_RIGHT;
        }

        if (state == VehicleState.FRONT_SEAT_MASS_SWITCH_LEFT) {
            return VehicleState.FRONT_SEAT_MASS_INTEN_LEFT;
        }

        if (state == VehicleState.FRONT_SEAT_MASS_SWITCH_RIGHT) {
            return VehicleState.FRONT_SEAT_MASS_INTEN_RIGHT;
        }

        return null;
    }

    // ------------------------------------------------------------------
    // Cluster theme
    // ------------------------------------------------------------------

    int[] clusterThemeSnapshot() {
        return mTheme.snapshot();
    }

    void registerClusterTheme(CanBusStubService canbus) {
        mTheme.register(canbus);
    }

    static String systemProp(String key, String def) {
        try {
            Class<?> sp = Class.forName("android.os.SystemProperties");

            return (String) sp.getMethod("get", String.class, String.class).invoke(null, key, def);
        } catch (ReflectiveOperationException e) {
            return def;
        }
    }

    // ------------------------------------------------------------------
    // ACC request
    // ------------------------------------------------------------------

    /**
     * Journals a head-unit power request of the screen lifecycle; false when it repeats the last
     * request.
     */
    public boolean requestAcc(int value) {
        if (value != 0 && value != 2) {
            throw new IllegalArgumentException("unknown ACC state");
        }

        synchronized (mLock) {
            if (value == mAccRequest) {
                return false;
            }

            record("state_requested", null, "acc", value, mAccRequest);
            mAccRequest = value;

            return true;
        }
    }

    /** Journals an observed ACC before the component reports it. */
    public void accApplied(int value) {
        record("state_applied", null, "acc", value, null);
    }

    // ------------------------------------------------------------------
    // Climate
    // ------------------------------------------------------------------

    public AirCondition airCondition() {
        AirCondition air = new AirCondition();
        int power = airConditionState(AirConditionState.AC_POWER_SWITCH);

        air.setIGN_ON(1);
        air.setAirSWStatus(power);
        air.setAc_power_switch(power);
        air.setAirFrontSWStatus(power);
        air.setAirDisplaySW(1);
        air.setAirLeftTemperature(airConditionState(AirConditionState.AC_LEFT_TEMP) / 10f);
        air.setAirRightTemperature(airConditionState(AirConditionState.AC_RIGHT_TEMP) / 10f);
        air.setAirRearTemperature(airConditionState(AirConditionState.AC_REAR_TEMP) / 10f);
        air.setAirDUALStatus(airConditionState(AirConditionState.AC_DUAL));
        air.setAirDUALAllStatus(airConditionState(AirConditionState.AC_DUAL_ALL));
        air.setairworking_mode(airConditionState(AirConditionState.AC_WORKING_MODE));
        air.setAcAirVentCoolSts(airConditionState(AirConditionState.VENT_COOL_SET));
        air.setAirACStatus(airConditionState(AirConditionState.AC_SWITCH));
        air.setAirMode(airConditionState(AirConditionState.AC_AUTO));
        air.setAirCirculationStatus(airConditionState(AirConditionState.AC_RECIRC_AIR));
        air.setAirCirculationMode(airConditionState(AirConditionState.AC_RECIRC_AIR));
        air.setAirFrontWindowDefogger(airConditionState(AirConditionState.AC_FRONT_DEFROST_SWITCH));
        air.setAirRearWindowDefogger(airConditionState(AirConditionState.AC_REAR_DEFROST_SWITCH));
        air.setAirSupplyStatus(airConditionState(AirConditionState.AC_FLOW_MODE));
        // OEM setter levels are 1..8; feedback/seekbar levels are 0..7.
        air.setAirWindSpeed(Math.max(0, airConditionState(AirConditionState.AC_BLOWER) - 1));

        return air;
    }

    public int airConditionState(AirConditionState state) {
        int initial = 0;

        switch (state) {
            case AC_POWER_SWITCH:
            case AC_SWITCH:
            case AC_DUAL:
            case AC_DUAL_ALL:
            case AC_BLOWER:
                initial = 1;
                break;
            case AC_LEFT_TEMP:
            case AC_RIGHT_TEMP:
            case AC_REAR_TEMP:
                initial = 240;
                break;
            case AC_WORKING_MODE:
                initial = 2;
                break;
            default:
                break;
        }

        return mClimate.getInt(state.name(), initial);
    }

    private void writeClimate(SharedPreferences.Editor editor, AirConditionState state, int value) {
        // The OEM UI sends a pulse (1) for both directions of these switches.
        if (value == 1
                && (state == AirConditionState.AC_POWER_SWITCH
                        || state == AirConditionState.AC_DUAL
                        || state == AirConditionState.AC_DUAL_ALL
                        || state == AirConditionState.AC_SWITCH
                        || state == AirConditionState.AC_AUTO
                        || state == AirConditionState.AC_RECIRC_AIR
                        || state == AirConditionState.AC_FRONT_DEFROST_SWITCH
                        || state == AirConditionState.VENT_COOL_SET)) {
            value = airConditionState(state) == 0 ? 1 : 0;
        }

        editor.putInt(state.name(), value);

        if (state == AirConditionState.AC_DUAL && value == 0) {
            editor.putInt(
                    AirConditionState.AC_RIGHT_TEMP.name(),
                    airConditionState(AirConditionState.AC_LEFT_TEMP));
        }

        if (airConditionState(AirConditionState.AC_DUAL) == 0) {
            if (state == AirConditionState.AC_LEFT_TEMP) {
                editor.putInt(AirConditionState.AC_RIGHT_TEMP.name(), value);
            }

            if (state == AirConditionState.AC_RIGHT_TEMP) {
                editor.putInt(AirConditionState.AC_LEFT_TEMP.name(), value);
            }
        }
    }

    private void saveClimate(SharedPreferences.Editor editor) {
        if (!editor.commit()) {
            throw new IllegalStateException("Cannot save emulator climate state");
        }
    }

    public void setAirConditionState(AirConditionState state, int value) {
        synchronized (mClimate) {
            SharedPreferences.Editor editor = mClimate.edit();

            writeClimate(editor, state, value);
            saveClimate(editor);
        }

        respondAirCondition();
    }

    public int setAirConditionBundleState(Bundle bundle) {
        synchronized (mClimate) {
            SharedPreferences.Editor editor = mClimate.edit();

            for (AirConditionState state : AirConditionState.values()) {
                if (bundle.containsKey(state.name())) {
                    writeClimate(editor, state, bundle.getInt(state.name()));
                }
            }

            // Resolve sync after all values in the bundle: editor writes are not visible through
            // SharedPreferences until commit.
            int dual = airConditionState(AirConditionState.AC_DUAL);

            if (bundle.containsKey(AirConditionState.AC_DUAL.name())) {
                int command = bundle.getInt(AirConditionState.AC_DUAL.name());
                dual = command == 1 ? (dual == 0 ? 1 : 0) : command;
            }

            if (dual == 0) {
                int temperature = airConditionState(AirConditionState.AC_LEFT_TEMP);

                if (bundle.containsKey(AirConditionState.AC_LEFT_TEMP.name())) {
                    temperature = bundle.getInt(AirConditionState.AC_LEFT_TEMP.name());
                } else if (bundle.containsKey(AirConditionState.AC_RIGHT_TEMP.name())) {
                    temperature = bundle.getInt(AirConditionState.AC_RIGHT_TEMP.name());
                }

                editor.putInt(AirConditionState.AC_LEFT_TEMP.name(), temperature);
                editor.putInt(AirConditionState.AC_RIGHT_TEMP.name(), temperature);
            }

            saveClimate(editor);
        }

        respondAirCondition();

        return 1;
    }

    private void respondAirCondition() {
        mHandler.postDelayed(
                () -> mComponent.onAirConditionChanged(mComponent.getAirCondition()),
                SETTING_FEEDBACK_MS);
    }

    // ------------------------------------------------------------------
    // Phone information for the cluster
    // ------------------------------------------------------------------

    PhoneDeliveryDiagnostics phoneDiagnostics() {
        return mPhoneDiagnostics;
    }

    /** Head-unit information for the cluster over the one IPK link of the guest. */
    IpkInfoSync ipk() {
        return mIpk;
    }

    /** Publishes PhoneInfo over the IPK link to the cluster. */
    public void publishPhone(PhoneInfo p) {
        mPhoneDiagnostics.event(PhoneDeliveryDiagnostics.Event.SINK_RECEIVED);

        if (p == null || p.getPhoneState() == null) {
            mPhoneDiagnostics.event(PhoneDeliveryDiagnostics.Event.SINK_REJECTED);
            mPhoneDiagnostics.failure(PhoneDeliveryDiagnostics.Failure.SINK_VALUES);

            return;
        }

        try {
            long sequence =
                    mIpk.phone(
                            "canbus",
                            p.getPhoneState().name(),
                            p.isVehicleCall(),
                            p.getDuration(),
                            p.getName(),
                            p.getPhoneNum(),
                            (seq, submitted, stage, reason, ioClass) -> {
                                mPhoneDiagnostics.socket(submitted, stage, reason, ioClass);
                                Log.i(
                                        TAG,
                                        "phone cluster sequence="
                                                + seq
                                                + " socket_submitted="
                                                + submitted
                                                + " stage="
                                                + stage.name()
                                                + " reason="
                                                + reason.name()
                                                + " io_class="
                                                + ioClass.name());
                            });
            mPhoneDiagnostics.event(
                    sequence > 0
                            ? PhoneDeliveryDiagnostics.Event.ENQUEUED
                            : PhoneDeliveryDiagnostics.Event.NO_FRAMES);
            Log.i(
                    TAG,
                    "phone producer sequence="
                            + sequence
                            + " uid="
                            + android.os.Binder.getCallingUid());
        } catch (IllegalArgumentException | IllegalStateException rejected) {
            mPhoneDiagnostics.event(PhoneDeliveryDiagnostics.Event.SINK_REJECTED);
            mPhoneDiagnostics.failure(PhoneDeliveryDiagnostics.Failure.SINK_VALUES);
            Log.w(TAG, "phone producer rejected");
        }
    }
}
