package com.qinggan.vehicle;

import android.os.Binder;

import com.qinggan.canbus.service.VehicleFixtureState;

public class VehicleConfigHelper {
    public static int getVehicleTypeID() {
        if (!Binder.cleared) {
            throw new AssertionError("getter without system identity");
        }

        VehicleFixtureState.calls++;

        if (VehicleFixtureState.fail) {
            throw new IllegalStateException();
        }

        return VehicleFixtureState.value;
    }
}
