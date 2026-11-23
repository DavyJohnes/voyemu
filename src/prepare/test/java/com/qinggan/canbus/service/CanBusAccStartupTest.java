package com.qinggan.canbus.service;

import android.net.LocalSocket;

import com.qinggan.canbus.VehicleState;

import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;

/** An unsent first host ACC survives the time before the initial CAN2 lease. */
public final class CanBusAccStartupTest {
    public static void main(String[] args) throws Exception {
        CanModelBridge bridge = new CanModelBridge();
        List<Integer> observed = new CopyOnWriteArrayList<>();

        LocalSocket.initialAcc(0);
        LocalSocket.available(false);
        bridge.setHostAccStatus(2);
        bridge.setHostAccStatus(0);
        bridge.start(
                new CanModelBridge.Listener() {
                    @Override
                    public void onModelState(
                            CanModelBridge source,
                            VehicleState state,
                            int value,
                            long session,
                            long epoch,
                            long revision) {}

                    @Override
                    public void onModelResponse(
                            CanModelBridge source,
                            VehicleState state,
                            int value,
                            long session,
                            long epoch) {}

                    @Override
                    public void onModelAcc(CanModelBridge source, int value) {
                        observed.add(value);
                    }
                });

        try {
            Thread.sleep(700);
            CanBusAccStatusTest.check(
                    LocalSocket.requests(3) == 0, "initial host request sent without a lease");
            LocalSocket.available(true);
            CanBusAccStatusTest.await(
                    () -> bridge.getAccStatus() == 0 && LocalSocket.requests(3) == 1,
                    "latest initial OFF lost before lease");
            CanBusAccStatusTest.check(
                    observed.equals(List.of(0)), "initial OFF contained a transient ON");
            bridge.setHostAccStatus(2);
            CanBusAccStatusTest.await(
                    () -> bridge.getAccStatus() == 2, "normal host READY was not delivered");
            CanBusAccStatusTest.check(
                    observed.equals(List.of(0, 2)), "host OFF/READY callbacks differ");
            LocalSocket.available(false);
            CanBusAccStatusTest.await(
                    () -> bridge.getAccStatus() == -1, "expired transport retained known ACC");
            int sent = LocalSocket.requests(3);
            bridge.setHostAccStatus(0);
            LocalSocket.available(true);
            Thread.sleep(1200);
            CanBusAccStatusTest.check(
                    LocalSocket.requests(3) == sent,
                    "an uncertain later ACC was replayed after reconnect");
            CanBusAccStatusTest.check(
                    bridge.getAccStatus() == -1, "uncertain ACC was made known by replay");
        } finally {
            bridge.stop();
        }

        System.out.println(
                "first host ACC waits for lease; uncertain later requests do not replay");
    }
}
