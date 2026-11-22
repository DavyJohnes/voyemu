package com.qinggan.canbus.service;

import android.content.Context;
import android.net.LocalSocket;
import android.os.Handler;

import com.qinggan.account.AccountUserManager;
import com.qinggan.canbus.VehicleState;
import com.qinggan.canbus.service.protocol.dongfeng.DongfengCanBusComponentImpl;
import com.qinggan.provider.QGSettings;
import com.voyemu.prepare.PhonePeerJsonBridge;

import java.util.List;
import java.util.Map;
import java.util.Objects;

/**
 * The model ACC reaches the OEM power mode handler of the component: ACC ON sends the drive mode
 * packet of the account saved at the last ACC OFF, a repeated ACC sends nothing, and the mode
 * history keeps the accepted DRIVING_MODE_SET packets and IVI_SOC_MODESET commands in order with
 * the boot, their origin and its completeness. Argument: qinggan09 or qinggan11.
 */
public final class DriveModeStartupTest {
    static void check(boolean value, String message) {
        CanBusComponentTest.check(value, message);
    }

    @SuppressWarnings("unchecked")
    static Map<String, Object> history(ModeCommandHistory journal, String request)
            throws org.json.JSONException {
        return (Map<String, Object>) PhonePeerJsonBridge.parse(journal.history(request).toString());
    }

    static Map<String, Object> history(DongfengCanBusComponentImpl component, String request)
            throws org.json.JSONException {
        return history(component.state().modeHistory(), request);
    }

    @SuppressWarnings("unchecked")
    static List<Map<String, Object>> events(DongfengCanBusComponentImpl component)
            throws org.json.JSONException {
        return (List<Map<String, Object>>) history(component, "{\"schema\":1}").get("events");
    }

    /** The preferences are shared by all service instances across guest boots. */
    static void verifyBootHistory(Context context) throws org.json.JSONException {
        var preferences =
                context.getSharedPreferences("mode-history-boot-fixture", Context.MODE_PRIVATE);
        String request = "{\"schema\":1}";
        String boot = "1b4e28ba-2fa1-11d2-883f-0016d3cca427";
        ModeCommandHistory first = new ModeCommandHistory(preferences, boot, "first");
        Map<String, Object> initial = history(first, request);

        check(
                initial.get("complete").equals(true)
                        && initial.get("incomplete_reason") == null
                        && boot.equals(initial.get("boot_id")),
                "a first service of the boot has the complete history: " + initial);
        ModeCommandHistory restarted = new ModeCommandHistory(preferences, boot, "restarted");
        Map<String, Object> again = history(restarted, request);
        check(
                again.get("complete").equals(false)
                        && again.get("incomplete_reason").equals("service_restarted")
                        && ((List<?>) again.get("events")).isEmpty(),
                "a service restarted in the same boot misses the earlier commands: " + again);
        String nextBoot = "ebc1b5ad-a018-4a15-8e72-32f7e98647a1";
        ModeCommandHistory rebooted = new ModeCommandHistory(preferences, nextBoot, "rebooted");
        Map<String, Object> afterReboot = history(rebooted, request);
        check(
                afterReboot.get("complete").equals(true)
                        && nextBoot.equals(afterReboot.get("boot_id")),
                "a new boot does not inherit the previous completeness state: " + afterReboot);
    }

    static void awaitAcc(
            DongfengCanBusComponentImpl component, CanBusComponentTest.Observer observer, int acc)
            throws InterruptedException {
        CanBusComponentTest.await(
                () -> {
                    Handler.drain();

                    return component.getAccStatus() == acc
                            && observer.count("onAccStateChanged " + acc) > 0;
                },
                "model ACC reaches the component: " + acc);
    }

    static void verifyInvalidRequests(DongfengCanBusComponentImpl component)
            throws org.json.JSONException {
        for (String bad :
                new String[] {
                    "{}", "{\"schema\":2}", "{\"schema\":1,\"limit\":257}", "{\"schema\":1,\"x\":1}"
                }) {
            boolean rejected = false;

            try {
                history(component, bad);
            } catch (IllegalArgumentException expected) {
                rejected = true;
            }

            check(rejected, "invalid mode history request accepted: " + bad);
        }
    }

    static void verifyEnergyAdmission(
            DongfengCanBusComponentImpl component, CanBusComponentTest.Observer observer)
            throws Exception {
        LocalSocket.acceptStates(false);
        component.setVehicleState(VehicleState.IVI_SOC_MODESET, 5);
        CanBusComponentTest.await(
                () -> {
                    Handler.drain();

                    return observer.count("onVehicleStateSettingResponseEx IVI_SOC_MODESET 1") > 0;
                },
                "the rejected write reports current energy mode instead of the request");
        check(events(component).size() == 3, "a CAN2 rejection cannot become an accepted command");
        LocalSocket.acceptStates(true);

        // The separate energy mode command keeps its own kind once the model admits it.
        component.setVehicleState(VehicleState.IVI_SOC_MODESET, 4);
        CanBusComponentTest.await(
                () -> {
                    try {
                        return events(component).size() == 4;
                    } catch (org.json.JSONException failure) {
                        throw new IllegalStateException(failure);
                    }
                },
                "admitted IVI_SOC_MODESET must reach the history");
        Map<String, Object> energy = events(component).get(3);
        check(
                energy.get("command").equals("IVI_SOC_MODESET")
                        && energy.get("value").equals(4)
                        && energy.get("packet").equals(Map.of("IVI_SOC_MODESET", 4)),
                "IVI_SOC_MODESET is a separate command: " + energy);
    }

    public static void main(String[] args) throws Exception {
        boolean h97c = args[0].equals("qinggan11");
        Context context = new Context();
        DongfengCanBusComponentImpl component = new DongfengCanBusComponentImpl();
        CanBusComponentTest.Observer observer = new CanBusComponentTest.Observer();

        // Comfort was saved before; the guest account starts in Eco anyway.
        QGSettings.System.VALUES.put("DRIVING_MODE_SET", 2);
        QGSettings.Global.VALUES.put("vehicle_setting_drive_mode_atm", 1);
        component.init(context);
        component.attachObserver(observer.callback);
        Handler.drain();
        observer.take();
        LocalSocket.available(true);
        LocalSocket.acceptStates(true);
        component.start();
        awaitAcc(component, observer, 2);
        Map<String, Object> first = history(component, "{\"schema\":1}");
        List<Map<String, Object>> events = events(component);
        check(
                Objects.equals(component.state().bootId(), first.get("boot_id")),
                "the history is attributed to the boot of the component: " + first);
        check(events.size() == 1, "one ACC ON packet: " + events);
        Map<String, Object> eco = events.get(0);
        Map<String, Object> packet =
                h97c
                        ? Map.of("DRIVING_MODE_SET", 1, "EPS_MODE_SET", 2, "PROP_MODE_SET", 1)
                        : Map.of(
                                "DRIVING_MODE_SET",
                                1,
                                "EPS_MODE_SET",
                                2,
                                "PROP_MODE_SET",
                                1,
                                "IB_MODE_SET",
                                1,
                                "HUM_ENERGY_PTREGEN_LEVL",
                                4);
        check(
                eco.get("seq").equals("1")
                        && eco.get("command").equals("DRIVING_MODE_SET")
                        && eco.get("value").equals(1)
                        && eco.get("origin").equals("acc_on")
                        && eco.get("packet").equals(packet),
                "the guest ACC ON packet must carry Eco with the OEM steering, drive and"
                        + " regeneration: "
                        + eco);
        check(
                component.getVehicleState(VehicleState.DRIVING_MODE_SET) == 1
                        && QGSettings.System.VALUES.get("DRIVING_MODE_SET") == 1
                        && QGSettings.System.VALUES.get("ACC_STATUS") == 2,
                "the OEM saves the Eco start and ACC ON");
        check(
                component.getVehicleState(VehicleState.VEHICLE_AMBIENT_LIGHT_COLOR)
                        == (h97c ? 19 : 0),
                "only H97C selects the linked Eco colour");
        check(observer.count("onAccStateChanged 2") == 1, "ACC ON reaches the observers once");

        // The model bridge delivers each ACC once; a repeated ACC ON power mode of the next BCM
        // PEPS frame reaches the handler directly and does not start again.
        java.lang.reflect.Method frame =
                DongfengCanBusComponentImpl.class.getDeclaredMethod(
                        "onBcmPepsPowerMode", int.class);
        frame.setAccessible(true);
        frame.invoke(component, 2);
        Handler.drain();
        check(events(component).size() == 1, "a repeated ACC ON must not send a packet");
        check(observer.count("onAccStateChanged 2") == 1, "a repeated ACC ON is silent");

        component.onVehicleStateChanged(VehicleState.DRIVING_MODE_SET, 2);
        Handler.drain();
        check(events(component).size() == 1, "a UI notification is not a model write");
        check(
                component.getVehicleState(VehicleState.DRIVING_MODE_SET) == 1,
                "a UI notification does not change the accepted mode");
        observer.take();

        // A logged-in user selects Comfort; the next ACC ON starts in Comfort without Eco first.
        component.setVehicleState(VehicleState.DRIVING_MODE_SET, 2);
        AccountUserManager.guest = false;
        component.setAccStatus(0);
        awaitAcc(component, observer, 0);
        check(
                QGSettings.System.VALUES.get("VehicleAccountInfo") == 2
                        && QGSettings.System.VALUES.get("ACC_STATUS") == 0,
                "ACC OFF saves the logged-in account");
        observer.take();
        component.setAccStatus(2);
        awaitAcc(component, observer, 2);
        check(
                observer.count("onVehicleStateChanged DRIVING_MODE_SET 2") == (h97c ? 2 : 1),
                "H97C publishes the restored drive mode besides the packet feedback");
        events = events(component);
        check(events.size() == 3, "client Comfort and the second ACC ON packet: " + events);
        check(
                events.get(1).get("origin").equals("client")
                        && events.get(1).get("value").equals(2)
                        && events.get(2).get("origin").equals("acc_on")
                        && events.get(2).get("value").equals(2)
                        && events.get(2).get("seq").equals("3"),
                "the logged-in ACC ON packet must carry the saved Comfort: " + events);
        check(
                component.getVehicleState(VehicleState.VEHICLE_AMBIENT_LIGHT_COLOR)
                        == (h97c ? 5 : 0),
                "the subscribers and the linked colour agree on Comfort only on H97C");

        verifyEnergyAdmission(component, observer);
        component.stop();

        // Cursors continue, a replaced source restarts, a restart in the same boot is incomplete.
        Map<String, Object> next = history(component, "{\"schema\":1,\"limit\":1}");
        @SuppressWarnings("unchecked")
        Map<String, Object> cursor = (Map<String, Object>) next.get("next_cursor");
        check(cursor.get("after_seq").equals("1"), "limit bounds the events: " + next);
        Map<String, Object> rest =
                history(
                        component,
                        "{\"schema\":1,\"cursor\":{\"source_instance\":\""
                                + cursor.get("source_instance")
                                + "\",\"after_seq\":\"1\"}}");
        check(
                ((List<?>) rest.get("events")).size() == 3 && rest.get("gap") == null,
                "the cursor continues after its event: " + rest);
        check(
                history(
                                component,
                                "{\"schema\":1,\"cursor\":{\"source_instance\":\"other\","
                                        + "\"after_seq\":\"4\"}}")
                        .get("gap")
                        .equals("source_replaced"),
                "another service instance is a gap");
        verifyBootHistory(context);

        verifyInvalidRequests(component);

        System.out.println(
                "DriveModeStartupTest " + args[0] + ": " + CanBusComponentTest.checks + " checks");
    }
}
