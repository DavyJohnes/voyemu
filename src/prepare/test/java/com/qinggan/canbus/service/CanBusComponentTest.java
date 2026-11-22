package com.qinggan.canbus.service;

import android.content.Context;
import android.net.LocalSocket;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;

import com.qinggan.canbus.AirConditionState;
import com.qinggan.canbus.GearState;
import com.qinggan.canbus.GuideState;
import com.qinggan.canbus.ICanBusServiceCallback;
import com.qinggan.canbus.MediaSrcInfo;
import com.qinggan.canbus.MediaSrcType;
import com.qinggan.canbus.MediaState;
import com.qinggan.canbus.NaviInfo;
import com.qinggan.canbus.PhoneInfo;
import com.qinggan.canbus.PhoneState;
import com.qinggan.canbus.VehicleState;
import com.qinggan.canbus.service.protocol.dongfeng.DongfengCanBusComponentImpl;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import java.util.function.BooleanSupplier;

/**
 * The CAN bus component owns the observers and the vehicle state: getters read the maintained
 * state, observers hear transitions only, writes reach the state, the model and the cluster, the
 * head-unit information reaches the IPK link, the transport uses a registered operator, and reads
 * change nothing.
 */
public final class CanBusComponentTest {
    static int checks;

    static void check(boolean value, String message) {
        checks++;

        if (!value) {
            throw new AssertionError(message);
        }
    }

    static void await(BooleanSupplier condition, String message) throws InterruptedException {
        long end = System.nanoTime() + 5_000_000_000L;

        while (!condition.getAsBoolean() && System.nanoTime() < end) {
            Thread.sleep(10);
        }

        check(condition.getAsBoolean(), message);
    }

    /** Observer binder whose death the test triggers. */
    static final class TestBinder implements IBinder {
        DeathRecipient death;

        @Override
        public void linkToDeath(DeathRecipient recipient, int flags) {
            death = recipient;
        }

        @Override
        public boolean unlinkToDeath(DeathRecipient recipient, int flags) {
            death = null;

            return true;
        }
    }

    /** Records every callback an observer receives as "method arg...". */
    static final class Observer {
        final TestBinder binder = new TestBinder();
        final List<String> calls = new ArrayList<>();
        final ICanBusServiceCallback callback;

        Observer() {
            InvocationHandler handler =
                    (proxy, method, args) -> {
                        if (method.getName().equals("asBinder")) {
                            return binder;
                        }

                        StringBuilder call = new StringBuilder(method.getName());

                        for (Object arg : args == null ? new Object[0] : args) {
                            call.append(' ')
                                    .append(
                                            arg instanceof Number
                                                            || arg instanceof Enum
                                                            || arg instanceof Boolean
                                                    ? arg
                                                    : "obj");
                        }

                        synchronized (calls) {
                            calls.add(call.toString());
                        }

                        return null;
                    };

            callback =
                    (ICanBusServiceCallback)
                            Proxy.newProxyInstance(
                                    ICanBusServiceCallback.class.getClassLoader(),
                                    new Class<?>[] {ICanBusServiceCallback.class},
                                    handler);
        }

        List<String> take() {
            synchronized (calls) {
                List<String> result = new ArrayList<>(calls);
                calls.clear();

                return result;
            }
        }

        long count(String prefix) {
            synchronized (calls) {
                return calls.stream().filter(c -> c.startsWith(prefix)).count();
            }
        }
    }

    /** Records the commands sent through the CAN box operator. */
    static final class Operator implements ICanBusOperator {
        final List<String> sent = new ArrayList<>();

        @Override
        public int getCanBusState() {
            return 1;
        }

        @Override
        public int sendCanCmd(int fd, int command, byte[] data) {
            sent.add("can " + fd + " " + command + " " + Arrays.toString(data));

            return 7;
        }

        @Override
        public byte[] sendCanCmdBytes(int fd, int command, byte[] data) {
            sent.add("bytes " + fd + " " + command);

            return new byte[] {1, 2};
        }

        @Override
        public int sendMuCmd(int fd, int command, byte[] data) {
            sent.add("mu " + fd + " " + command);

            return 8;
        }

        @Override
        public int sendSocketCmd(int command, byte[] data) {
            sent.add("socket " + command);

            return 9;
        }
    }

    static String inputs(long sequence, int gear, int speed) throws org.json.JSONException {
        return inputs(sequence, gear, speed, 0, 0, 0);
    }

    static String inputs(long sequence, int gear, int speed, int rpm, int brake, int oil)
            throws org.json.JSONException {
        org.json.JSONObject raw = new org.json.JSONObject();

        for (String name : VehicleSignalCatalog.NAMES) {
            raw.put(name, 0);
        }

        raw.put("gear_valid", 1).put("gear", gear).put("speed", speed).put("speed10x", speed * 10);
        raw.put("rpm", rpm).put("brake_oil_low", brake).put("engine_oil_low", oil);

        return new org.json.JSONObject()
                .put("schema", 1)
                .put("map_sha256", VehicleSignalCatalog.SHA256)
                .put("origin", "manual-emulator")
                .put("updated_utc", "2026-01-01T00:00:00Z")
                .put("sequence", sequence)
                .put("raw_oem", raw)
                .toString();
    }

    /**
     * Getters and observer callbacks follow the maintained vehicle input; equal input is silent.
     */
    static void vehicleInput(DongfengCanBusComponentImpl component, Observer observer)
            throws org.json.JSONException {
        CanBusState state = component.state();

        check(state.applyVehicleSnapshot(1, 50, 80, 1, 0, 1), "legacy snapshot rejected");
        Handler.drain();
        check(
                observer.take()
                        .equals(
                                List.of(
                                        "onVehicleSpeedChanged 50",
                                        "onBatteryRemainingCapacityChanged 80.0",
                                        "onDoorStatusChanged obj",
                                        "onLightStatusChanged obj")),
                "first snapshot must report each changed callback once");
        check(component.getVehicleSpeed() == 50, "speed getter must read the snapshot");
        check(component.getBatteryRemainingCapacity() == 80f, "SOC getter must read the snapshot");
        check(component.getDoorStatus().fLDoor == 1, "door getter must read the snapshot");
        check(component.getLightStatus().directionR == 1, "light getter must read the snapshot");
        check(state.applyVehicleSnapshot(2, 50, 81, 1, 0, 1), "second snapshot rejected");
        Handler.drain();
        check(
                observer.take().equals(List.of("onBatteryRemainingCapacityChanged 81.0")),
                "only the changed SOC may be reported");
        check(state.applyVehicleSnapshot(3, 50, 81, 1, 0, 1), "equal snapshot rejected");
        Handler.drain();
        check(observer.take().isEmpty(), "an unchanged snapshot must not report anything");
        check(!state.applyVehicleSnapshot(3, 51, 81, 1, 0, 1), "stale sequence accepted");

        check(state.applyVehicleInputs(inputs(10, 3, 50)), "complete input rejected");
        Handler.drain();
        List<String> first = observer.take();
        check(
                first.contains("onGearStatusChanged Drive")
                        && !first.contains("onVehicleSpeedChanged 50"),
                "complete input must report the new gear and keep the unchanged speed silent");
        check(component.getGearStatus() == GearState.Drive, "gear getter must read the input");
        check(state.applyVehicleInputs(inputs(11, 1, 50)), "second complete input rejected");
        Handler.drain();
        check(
                observer.take().equals(List.of("onGearStatusChanged Reverse")),
                "only the changed gear may be reported");
        check(state.applyVehicleInputs(inputs(11, 1, 50)), "idempotent input must be accepted");
        Handler.drain();
        check(observer.take().isEmpty(), "a repeated input must not report anything");
    }

    static void same(Object actual, Object expected, String name) {
        check(
                java.util.Objects.equals(actual, expected),
                name + " is " + actual + ", not " + expected);
    }

    /** Without vehicle data the getters return what the OEM component returns before its data. */
    static void initialValues(DongfengCanBusComponentImpl component) {
        same(component.getEngineSpeed(), Integer.MIN_VALUE, "engine speed");
        same(component.getBrakeFluidStatus(), -1, "brake fluid");
        same(component.getEngineFluidStatus(), -1, "engine fluid");
        same(component.getVehicleSpeed(), -1, "speed");
        same(component.getBatteryRemainingCapacity(), -1f, "SOC");
        same(component.getAmbientTemperatureValue(), Integer.MIN_VALUE, "ambient");
        same(component.getGearStatus(), GearState.Unknown, "gear");
        same(component.getEngineStatus(), -1, "engine status");
        same(component.getVehicleKeyState(), -1, "key state");
        same(component.getHevSysMode(), 0, "HEV mode");
        same(component.getAlarmData(), -1, "alarm");
        same(component.getIllumination(), -1, "illumination");
        same(component.getHandBrakeStatus(), -1, "hand brake");
        same(component.getWiperFluidStatus(), -1, "wiper fluid");
        same(component.getComprehensiveFuelConsumption(), -1, "consumption");
        same(component.getSecondaryOdometer(), -1L, "secondary odometer");
        same(component.getEngineTemperature(), 0, "engine temperature");
        same(component.getReTracking(), -1, "re-tracking");
        same(component.getRainfallLevel(), 0, "rainfall");
        same(component.getVehicleSceneMode(), 0, "scene mode");
        same(component.getInstantFuelConsumption(), 0, "instant consumption");
        same(component.getComprehensiveFuelConsumptionEX(), 0f, "average consumption");
        Object[] absent = {
            component.getCanBoxVersion(),
            component.getVehicleIO(),
            component.getSeatBeltStatus(),
            component.getFrontRadarData(),
            component.getSWCAngle(),
            component.getWheelSpeed(),
            component.getWheelCountValue(),
            component.getBrakePadStatus(),
            component.getRealityWarningInfo(),
            component.getEnergyConsumptionInfo()
        };

        for (Object value : absent) {
            same(value, null, "an OEM object without data");
        }

        check(component.getBatteryState() != null, "battery state exists from construction");
        check(component.getWindowStatus() != null, "window status exists from construction");
        same(component.getLightStatus().headLightLevel, -1, "untouched light status");
        same(component.getOdometer().mPowerConsumption, 0f, "untouched odometer");
        same(component.getFuelLevel().mPercentage, 0f, "untouched fuel level");
        same(
                component.isSupportFunction(com.qinggan.canbus.FunctionType.VehicleState),
                false,
                "CAN box function");
        same(
                component.setVehicleCenterControlEnabled(
                        com.qinggan.canbus.VehicleCenterControlEnabled.ITEM, 1),
                false,
                "center control write");
        same(
                component.getVehicleCenterControlEnabled(
                        com.qinggan.canbus.VehicleCenterControlEnabled.ITEM),
                -1,
                "center control");
        same(
                component.requestTravellingInfo(com.qinggan.canbus.TravellingInfoType.TRIP),
                -1,
                "travelling request");
        same(component.requestVehicleCenterControlEnabledInfo(), -1, "center control request");
    }

    /** Vehicle inputs without a source in the OEM Dongfeng protocol leave the OEM values. */
    static void unfilledSignals(DongfengCanBusComponentImpl component, Observer observer)
            throws org.json.JSONException {
        observer.take();
        check(
                component.state().applyVehicleInputs(inputs(20, 1, 50, 3000, 1, 1)),
                "input rejected");
        Handler.drain();
        same(observer.take(), List.of(), "callbacks for signals the OEM component never fills");
        same(component.getEngineSpeed(), Integer.MIN_VALUE, "engine speed with rpm");
        same(component.getBrakeFluidStatus(), -1, "brake fluid with brake_oil_low");
        same(component.getEngineFluidStatus(), -1, "engine fluid with engine_oil_low");
        same(component.getHandBrakeStatus(), -1, "hand brake with input");
        same(component.getSeatBeltStatus(), null, "seat belts with input");
    }

    /** A setting write answers with the response, its Ex form and the changed values. */
    static void settings(DongfengCanBusComponentImpl component, Observer observer) {
        component.setVehicleState(VehicleState.DRIVING_MODE_SET, 3);
        check(component.getVehicleState(VehicleState.DRIVING_MODE_SET) == 3, "setting not kept");
        Handler.drain();
        check(
                observer.take()
                        .equals(
                                List.of(
                                        "onVehicleStateSettingResponse DRIVING_MODE_SET true",
                                        "onVehicleStateSettingResponseEx DRIVING_MODE_SET 1",
                                        "onVehicleStateChanged DRIVING_MODE_SET 3")),
                "setting feedback differs from the OEM order");
        Bundle bundle = new Bundle();
        bundle.putInt(VehicleState.FRONT_SEAT_HEATING_COMMAND_LEFT.name(), 2);
        check(component.setVehicleBundleState(bundle) == 1, "bundle write not acknowledged");
        check(
                component.getVehicleState(VehicleState.FRONT_SEAT_HEATING_SWITCH_LEFT) == 2,
                "a seat level must switch the seat on");
        Handler.drain();
        check(
                observer.count("onVehicleStateChanged FRONT_SEAT_HEATING_SWITCH_LEFT 2") == 1,
                "paired seat switch must be reported");
        observer.take();
        component.setVehicleState(VehicleState.IPK_THEM_MODE, 4);
        check(component.getVehicleState(VehicleState.IPK_THEM_MODE) == 4, "theme mode not kept");
        Handler.drain();
        check(observer.count("onVehicleStateChanged IPK_THEM_MODE 4") == 1, "theme not reported");
        check(
                component.getVehicleState(VehicleState.IPK_THEME_SKIN) == 3001
                        && observer.count("onVehicleStateChanged IPK_THEME_SKIN 3001") == 1,
                "a mode-only theme write must select the default skin of that theme");
        observer.take();
        component.setVehicleState(VehicleState.IPK_THEM_MODE, 5);
        check(
                component.getVehicleState(VehicleState.IPK_THEME_SKIN) == 4001,
                "Harness World must select its default skin");
        component.setVehicleState(VehicleState.IPK_THEME_SKIN, 3003);
        check(
                component.getVehicleState(VehicleState.IPK_THEM_MODE) == 5
                        && component.getVehicleState(VehicleState.IPK_THEME_SKIN) == 3003,
                "a skin write must keep the mode");
        Handler.drain();
        observer.take();
        component.sendIPKNavideath();
        check(
                component.getVehicleState(VehicleState.IVI_AVM_NAVI_STATE) == 2,
                "navigation death must set the OEM navigation state");
        Handler.drain();
        observer.take();
        component.setAirConditionState(AirConditionState.AC_LEFT_TEMP, 220);
        check(
                component.getAirConditionState(AirConditionState.AC_LEFT_TEMP) == 220
                        && component.getAirConditionState(AirConditionState.AC_RIGHT_TEMP) == 240,
                "a dual-zone climate write must change only its zone");
        check(
                Float.valueOf(22f)
                        .equals(component.getAirCondition().values.get("AirLeftTemperature")),
                "climate getter must read the written temperature");
        Handler.drain();
        check(observer.take().equals(List.of("onAirConditionChanged obj")), "climate not reported");
        check(
                com.qinggan.provider.QGSettings.System.VALUES.get("DRIVING_MODE_SET") == 3,
                "a written drive mode must be saved to QGSettings as the OEM saves it");
        check(
                component.initVehicleLastStatus(null, VehicleState.SREV_SOC_SET, 5) == 5,
                "without a saved value the default must be cached");
        check(component.getVehicleState(VehicleState.SREV_SOC_SET) == 5, "cache not read");
    }

    /** The CAN box transport uses a registered operator and reports its absence as the OEM. */
    private static List<Integer> commands(List<byte[][]> publications) {
        List<Integer> result = new ArrayList<>();

        for (byte[][] frames : publications) {
            for (byte[] frame : frames) {
                result.add(frame[5] & 255);
            }
        }

        return result;
    }

    /** The OEM update entry points and command 0 of sendSocketCtl queue IVC frames on the link. */
    static void ipkEntries(DongfengCanBusComponentImpl component) {
        IpkLink link = component.state().ipk().link();
        MediaSrcInfo media = new MediaSrcInfo();

        media.source = MediaSrcType.USB;
        media.state = MediaState.PLAY;
        media.song = "Song";
        media.artist = "Artist";
        component.updateMediaSrcInfo(media);
        component.updateMediaSrcInfo(null);
        NaviInfo navi = new NaviInfo();
        navi.guide = GuideState.ROAM;
        navi.currentRoad = "Road";
        component.updateNaviInfo(navi);
        Bundle lanes = new Bundle();
        lanes.putInt("0", 7);
        component.updateNaviLaneInfo(true, lanes);
        component.updateCallListInfo(new Bundle());
        check(component.sendSocketCtl(0, IpkFrames.heartbeat()) == 0, "IVC frame not queued");
        check(component.sendSocketCtl(0, new byte[] {10, 16}) == -1, "partial frame queued");
        List<Integer> sent = commands(link.pendingFrames());
        check(
                sent.equals(List.of(0x6e, 0x6e, 0x68, 0x77, 0x76, 0x67)),
                "update entries must queue media, roaming, lanes and the raw frame: " + sent);
        byte[] lane = link.pendingFrames().get(3)[0];
        check(lane[6] == 1 && lane[8] == 7, "lane id");
    }

    static void transport(DongfengCanBusComponentImpl component) {
        check(component.sendCanIOCtl(1, new byte[] {3}) == -1, "missing operator must return -1");
        check(component.sendCanIOCtlBytes(1, new byte[] {3}) == null, "missing operator bytes");
        check(component.sendMuIoCtl(1, new byte[] {3}) == -1, "missing operator MU command");
        check(component.sendSocketCtl(1, new byte[] {3}) == -1, "missing operator socket");
        Operator operator = new Operator();
        component.registerOperator(operator, 11, 12);
        component.registerOperator(new Operator(), 13);
        check(component.sendCanIOCtl(5, new byte[] {4, 5}) == 7, "operator result not returned");
        check(
                Arrays.equals(component.sendCanIOCtlBytes(6, new byte[0]), new byte[] {1, 2}),
                "operator bytes not returned");
        check(component.sendMuIoCtl(7, new byte[0]) == 8, "MU result not returned");
        check(component.sendSocketCtl(8, new byte[0]) == 9, "socket result not returned");
        check(
                operator.sent.equals(
                        List.of("can 13 5 [4, 5]", "bytes 13 6", "mu 12 7", "socket 8")),
                "the first operator must receive the commands on the registered devices: "
                        + operator.sent);
        check(component.getCanRawData(1526) == null, "no raw CAN frame was received");
        check(
                !component.notify(1, (int[]) null) && !component.notify(1, new int[] {1}),
                "raw frames must not be dispatched without a decoder");
        check(
                Arrays.equals(component.getRawData(new int[] {1, 2, 3, 4}, 1, 2), new int[] {2, 3}),
                "raw frame copy differs");
        check(component.getIpkSyncComponent() == null, "no owned IPK readiness service");
        Locale previous = Locale.getDefault();

        try {
            Locale.setDefault(Locale.US);
            check(component.getSystemLanguage() == BaseCanBusComponent.LANG_EN, "English");
            Locale.setDefault(Locale.CHINA);
            check(component.getSystemLanguage() == BaseCanBusComponent.LANG_CN, "Chinese");
            Locale.setDefault(Locale.GERMANY);
            check(component.getSystemLanguage() == BaseCanBusComponent.LANG_UNKNOWN, "other");
        } finally {
            Locale.setDefault(previous);
        }
    }

    /** The OEM DVR view request and its forwarded touch are kept, journaled and acknowledged. */
    static void dvrSignals(DongfengCanBusComponentImpl component, Observer observer) {
        long journal = component.state().journal().latest();

        component.setVehicleState(VehicleState.DVR_DISPLAY_REQ, 1);
        Bundle touch = new Bundle();
        touch.putInt(VehicleState.DVR_TOUCH_MOTION.name(), 1);
        touch.putInt(VehicleState.DVR_COORDINATE_X.name(), 640);
        touch.putInt(VehicleState.DVR_COORDINATE_Y.name(), 360);
        check(
                component.setVehicleAndAirConditionBundleState(null, touch) == 1,
                "DVR touch not acknowledged");
        check(
                component.getVehicleState(VehicleState.DVR_DISPLAY_REQ) == 1
                        && component.getVehicleState(VehicleState.DVR_TOUCH_MOTION) == 1
                        && component.getVehicleState(VehicleState.DVR_COORDINATE_X) == 640
                        && component.getVehicleState(VehicleState.DVR_COORDINATE_Y) == 360,
                "DVR signals not kept");
        check(component.state().journal().latest() > journal, "DVR signals not journaled");
        Handler.drain();
        check(
                observer.count("onVehicleStateSettingResponse DVR_DISPLAY_REQ true") == 1
                        && observer.count("onVehicleStateChanged DVR_COORDINATE_X 640") == 1,
                "DVR signals not acknowledged");
        observer.take();
    }

    /** Reads open no model socket, send no command and add no journal event. */
    static void readsOnly(DongfengCanBusComponentImpl component) {
        CanBusState state = component.state();
        String self = Thread.currentThread().getName();
        int connects = LocalSocket.connectsFrom(self),
                commands = LocalSocket.requests(2) + LocalSocket.requests(3);
        long journal = state.journal().latest();

        for (int i = 0; i < 1000; i++) {
            component.getVehicleState(VehicleState.HUM_VSP_FUNCTION_SW);
            component.getVehicleState(VehicleState.DRIVING_MODE_SET);
            component.getAccStatus();
            component.getLightStatus();
            component.getOdometer();
            component.getTPMSInfo();
            component.getAirCondition();
        }

        check(LocalSocket.connectsFrom(self) == connects, "a read opened a model socket");
        check(
                LocalSocket.requests(2) + LocalSocket.requests(3) == commands,
                "a read sent a command");
        check(state.journal().latest() == journal, "a read changed the journal");
        check(Handler.drain() == 0, "a read queued work");
    }

    /** The model ACC reaches the observers once per observed transition. */
    static void modelAcc(DongfengCanBusComponentImpl component, Observer observer)
            throws InterruptedException {
        LocalSocket.available(true);
        component.start();
        await(() -> component.getAccStatus() == 2, "model ACC ON must be observed");
        Thread.sleep(700);
        Handler.drain();
        check(observer.count("onAccStateChanged 2") == 1, "observed ACC must be delivered once");
        observer.take();
        LocalSocket.holdAcc(true);
        component.setAccStatus(0);
        component.setAccStatus(0);
        await(() -> LocalSocket.pendingAcc() == 0, "ACC OFF request not sent");
        Handler.drain();
        check(observer.count("onAccStateChanged") == 0, "a requested ACC must not be reported");
        LocalSocket.holdAcc(false);
        await(() -> component.getAccStatus() == 0, "model ACC OFF must be observed");
        Thread.sleep(700);
        Handler.drain();
        check(observer.take().contains("onAccStateChanged 0"), "ACC OFF not delivered");
        check(LocalSocket.requests(3) == 1, "a repeated ACC request must not be sent again");
        component.setAccStatus(2);
        await(() -> component.getAccStatus() == 2, "model ACC ON must be observed again");
        component.stop();
        check(component.getAccStatus() == -1, "a stopped model reports unknown ACC");
    }

    @SuppressWarnings("unchecked")
    static java.util.Map<String, Object> phoneCounts(Object counts) {
        java.util.Map<String, Object> expected =
                new java.util.LinkedHashMap<>((java.util.Map<String, Object>) counts);

        expected.replaceAll((name, value) -> 0);
        expected.put("sink_received", 1);
        expected.put("sink_rejected", 1);

        return expected;
    }

    public static void main(String[] args) throws Exception {
        DongfengCanBusComponentImpl component = new DongfengCanBusComponentImpl();

        component.init(new Context());
        Observer observer = new Observer();
        check(component.attachObserver(observer.callback), "observer not attached");
        check(component.attachObserver(observer.callback), "repeated attach must succeed");
        Handler.drain();
        List<String> initial = observer.take();
        check(
                initial.contains("onLightStatusChanged obj")
                        && initial.stream().noneMatch(c -> c.startsWith("onCanBoxVersionChange"))
                        && initial.stream().noneMatch(c -> c.startsWith("onAccStateChanged"))
                        && initial.stream().noneMatch(c -> c.contains("HUM_VSP_FUNCTION_SW")),
                "the initial push must send the state but no unknown model values: " + initial);
        readsOnly(component);
        initialValues(component);
        vehicleInput(component, observer);
        unfilledSignals(component, observer);
        settings(component, observer);
        dvrSignals(component, observer);
        ipkEntries(component);
        transport(component);
        component.updatePhoneInfo(new PhoneInfo());
        Object counts = component.state().phoneDiagnostics().snapshot().get("counts");
        check(
                counts.equals(phoneCounts(counts)),
                "PhoneInfo without a call state must reach the cluster publisher and be rejected: "
                        + counts);
        PhoneInfo connected = new PhoneInfo();
        connected.state = PhoneState.CONNECTED;
        component.updatePhoneInfo(connected);
        List<byte[][]> queued = component.state().ipk().link().pendingFrames();
        check(
                queued.size() == 6 && commands(queued.subList(5, 6)).equals(List.of(0x6a)),
                "PhoneInfo must reach the IPK link");
        modelAcc(component, observer);
        Observer second = new Observer();
        component.attachObserver(second.callback);
        Handler.drain();
        second.take();
        check(component.detachObserver(observer.callback), "observer not detached");
        check(!component.detachObserver(observer.callback), "repeated detach must report false");
        second.binder.death.binderDied();
        component.onVehicleSpeedChanged(1);
        check(
                observer.take().isEmpty() && second.take().isEmpty(),
                "removed and dead observers must not be called");
        System.out.println(
                "CAN bus component state, observers and transport: " + checks + " checks");
    }
}
