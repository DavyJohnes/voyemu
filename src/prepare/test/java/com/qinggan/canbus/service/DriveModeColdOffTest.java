package com.qinggan.canbus.service;

import android.content.Context;
import android.net.LocalSocket;
import android.os.Handler;

import com.qinggan.canbus.service.protocol.dongfeng.DongfengCanBusComponentImpl;
import com.qinggan.provider.QGSettings;

import java.util.Map;

/** A cold host OFF emits no mode, then the first host ON restores the saved Comfort packet. */
public final class DriveModeColdOffTest {
    public static void main(String[] args) throws Exception {
        LocalSocket.initialAcc(0);
        LocalSocket.available(false);
        LocalSocket.acceptStates(true);
        DongfengCanBusComponentImpl component = new DongfengCanBusComponentImpl();
        CanBusComponentTest.Observer observer = new CanBusComponentTest.Observer();
        component.init(new Context());
        component.attachObserver(observer.callback);
        Handler.drain();
        component.start();

        try {
            CanBusComponentTest.check(
                    component.state().applyVehicleInputs(CanBusComponentTest.inputs(1, 0, 0), 0),
                    "initial host OFF rejected");
            Handler.drain();
            CanBusComponentTest.check(
                    DriveModeStartupTest.events(component).isEmpty(),
                    "unready cold model emitted a mode");
            LocalSocket.available(true);
            DriveModeStartupTest.awaitAcc(component, observer, 0);
            CanBusComponentTest.check(
                    DriveModeStartupTest.events(component).isEmpty(),
                    "cold OFF emitted a drive mode");
            QGSettings.System.VALUES.put("VehicleAccountInfo", 2);
            QGSettings.System.VALUES.put("DRIVING_MODE_SET", 2);
            QGSettings.Global.VALUES.put("vehicle_setting_drive_mode_atm", 1);
            String ready = CanBusComponentTest.inputs(2, 0, 0);
            CanBusComponentTest.check(
                    component.state().applyVehicleInputs(ready, 2), "host READY rejected");
            Handler.drain();
            DriveModeStartupTest.awaitAcc(component, observer, 2);
            CanBusComponentTest.await(
                    () -> {
                        Handler.drain();

                        try {
                            return DriveModeStartupTest.events(component).size() == 1;
                        } catch (org.json.JSONException failure) {
                            throw new IllegalStateException(failure);
                        }
                    },
                    "saved Comfort packet not observed");
            Map<String, Object> first = DriveModeStartupTest.events(component).get(0);
            CanBusComponentTest.check(
                    first.get("seq").equals("1")
                            && first.get("value").equals(2)
                            && first.get("origin").equals("acc_on"),
                    "first packet did not restore Comfort: " + first);
            CanBusComponentTest.check(
                    component.state().applyVehicleInputs(ready, 2), "idempotent READY rejected");
            Handler.drain();
            Thread.sleep(700);
            Handler.drain();
            CanBusComponentTest.check(
                    DriveModeStartupTest.events(component).size() == 1,
                    "duplicate READY emitted another mode");
        } finally {
            component.stop();
        }

        System.out.println("cold OFF remains mode-free; first host READY restores Comfort once");
    }
}
