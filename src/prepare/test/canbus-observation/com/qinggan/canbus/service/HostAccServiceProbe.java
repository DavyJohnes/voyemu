package com.qinggan.canbus.service;

import android.content.BroadcastReceiver;
import android.content.Intent;

import java.lang.reflect.Field;
import java.util.List;

/** Exercises actual service dispatch for the host-controlled and legacy screen power routes. */
public final class HostAccServiceProbe {
    private static final class HostService extends RegistrationService {
        @Override
        protected boolean hostControlsAcc() {
            return true;
        }
    }

    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static BroadcastReceiver receiver(RegistrationService service) throws Exception {
        Field field = RegistrationService.class.getDeclaredField("powerReceiver");

        field.setAccessible(true);

        return (BroadcastReceiver) field.get(service);
    }

    public static void main(String[] args) throws Exception {
        CanBusStubService binder = new CanBusStubService(null, null);
        Field field = RegistrationService.class.getDeclaredField("sBinder");

        field.setAccessible(true);
        field.set(null, binder);

        try {
            RegistrationService host = new HostService();
            BroadcastReceiver power = receiver(host);
            power.onReceive(host, new Intent(Intent.ACTION_SCREEN_ON));
            power.onReceive(host, new Intent(Intent.ACTION_SCREEN_OFF));
            check(binder.power.isEmpty(), "screen event replaced host ACC");
            Intent input =
                    new Intent("com.voyemu.canbus.APPLY_PERSISTENT_VEHICLE_STATE")
                            .putExtra("state_json", "rejected")
                            .putExtra("acc", 2);
            host.onStartCommand(input, 0, 1);
            check(
                    binder.hostPower.isEmpty() && binder.power.isEmpty(),
                    "rejected complete state requested ACC");
            binder.accept = true;
            host.onStartCommand(input, 0, 2);
            check(
                    binder.hostPower.equals(List.of(2)) && binder.power.isEmpty(),
                    "accepted host ACC bypassed full-state path");
            power.onReceive(host, new Intent(Intent.ACTION_SCREEN_ON));
            power.onReceive(host, new Intent(Intent.ACTION_SCREEN_OFF));
            check(binder.power.isEmpty(), "later screen event replaced host ACC");
            RegistrationService legacy = new RegistrationService();
            receiver(legacy).onReceive(legacy, new Intent(Intent.ACTION_SCREEN_ON));
            receiver(legacy).onReceive(legacy, new Intent(Intent.ACTION_SCREEN_OFF));
            check(binder.power.equals(List.of(2, 0)), "legacy screen power changed");
        } finally {
            field.set(null, null);
        }

        System.out.println("host power rejects screen events; legacy screen power retained");
    }
}
