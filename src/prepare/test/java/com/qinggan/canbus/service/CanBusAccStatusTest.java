package com.qinggan.canbus.service;

import android.net.LocalSocket;

import com.qinggan.canbus.VehicleState;

import java.util.Arrays;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.function.BooleanSupplier;

/** The model bridge reports only observed model ACC and reads without model exchanges. */
public final class CanBusAccStatusTest {
    private static final List<Integer> DELIVERED = new CopyOnWriteArrayList<>();
    private static volatile boolean deliveryDiffers;

    static void check(boolean value, String message) {
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

    static int bridgeThreads() {
        int count = 0;

        for (Thread thread : Thread.getAllStackTraces().keySet()) {
            count += "can-model-bridge".equals(thread.getName()) ? 1 : 0;
        }

        return count;
    }

    /** Repeated reads open no socket, send no command, start no worker and change no identity. */
    static void readsOnly(CanModelBridge component, int expected) {
        String self = Thread.currentThread().getName();
        int connects = LocalSocket.connectsFrom(self),
                commands = LocalSocket.requests(2) + LocalSocket.requests(3),
                threads = bridgeThreads();
        long[] identity = LocalSocket.identity();

        for (int i = 0; i < 10_000; i++) {
            check(component.getAccStatus() == expected, "getAccStatus must stay " + expected);
        }

        check(LocalSocket.connectsFrom(self) == connects, "getAccStatus opened a model socket");
        check(
                LocalSocket.requests(2) + LocalSocket.requests(3) == commands,
                "getAccStatus sent a model command");
        check(bridgeThreads() == threads, "getAccStatus started a worker");
        check(Arrays.equals(LocalSocket.identity(), identity), "getAccStatus changed the session");
    }

    static void recover(CanModelBridge component) throws InterruptedException {
        component.setAccStatus(0);
        await(() -> LocalSocket.acc() == 0, "recovery OFF not applied by the model");
        Thread.sleep(700);
        check(component.getAccStatus() == -1, "recovery OFF alone must not confirm ACC");
        component.setAccStatus(2);
        await(() -> component.getAccStatus() == 2, "OFF/ON recovery must restore observed ACC");
    }

    public static void main(String[] args) throws Exception {
        CanModelBridge component = new CanModelBridge();
        CanModelBridge.Listener listener =
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
                        deliveryDiffers |= source.getAccStatus() != value;
                        DELIVERED.add(value);
                    }
                };

        CanModelBridge next = new CanModelBridge();

        try {
            check(component.getAccStatus() == -1, "unstarted component must report unknown ACC");
            LocalSocket.available(false);
            component.start(listener);
            Thread.sleep(700);
            readsOnly(component, -1);
            LocalSocket.available(true);
            await(() -> component.getAccStatus() == 2, "first model observation must report ON");
            await(() -> DELIVERED.equals(List.of(2)), "first observed ACC must be delivered once");
            readsOnly(component, 2);

            LocalSocket.holdAcc(true);
            component.setAccStatus(0);
            await(() -> LocalSocket.pendingAcc() == 0, "ACC OFF command not accepted");
            Thread.sleep(700);
            readsOnly(component, 2);
            LocalSocket.holdAcc(false);
            await(() -> component.getAccStatus() == 0, "observed ACC OFF must be reported");
            check(
                    component.getVehicleState(VehicleState.HUM_VSP_FUNCTION_SW) == -1,
                    "ACC OFF must invalidate model getters");
            component.setAccStatus(2);
            await(() -> component.getAccStatus() == 2, "observed ACC ON must be reported");
            await(() -> DELIVERED.equals(List.of(2, 0, 2)), "ACC transitions must be delivered");

            LocalSocket.available(false);
            await(() -> component.getAccStatus() == -1, "lost transport must expire ACC");
            LocalSocket.available(true);
            await(() -> component.getAccStatus() == 2, "reconnected transport must report ACC");
            Thread.sleep(700);
            check(DELIVERED.size() == 3, "unchanged ACC after reconnect must not be redelivered");

            LocalSocket.rejectAcc(true);
            component.setAccStatus(0);
            await(
                    () -> component.getAccStatus() == -1,
                    "rejected ACC command must leave ACC unknown");
            LocalSocket.rejectAcc(false);
            check(LocalSocket.acc() == 2, "rejected ACC command changed the model");
            readsOnly(component, -1);
            recover(component);

            LocalSocket.loseSource();
            await(
                    () -> component.getAccStatus() == -1,
                    "unknown model ACC must be reported unknown");
            recover(component);

            LocalSocket.restart();
            await(() -> component.getAccStatus() == -1, "new model session must leave ACC unknown");
            Thread.sleep(700);
            readsOnly(component, -1);
            recover(component);
            check(!deliveryDiffers, "delivered ACC must equal getAccStatus");
            check(DELIVERED.equals(List.of(2, 0, 2)), "uncertain ACC must not be delivered");

            component.stop();
            check(component.getAccStatus() == -1, "stopped component must report unknown ACC");
            next.start(listener);
            Thread.sleep(1200);
            readsOnly(next, -1);
            recover(next);
        } finally {
            component.stop();
            next.stop();
        }

        System.out.println(
                "model bridge startup, OFF/ON, reconnect, uncertain feedback and restart report"
                        + " observed ACC without commands");
    }
}
