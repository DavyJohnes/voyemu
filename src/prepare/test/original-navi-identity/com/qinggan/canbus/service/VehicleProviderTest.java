package com.qinggan.canbus.service;

import android.content.ContentProvider;
import android.content.Context;
import android.os.Binder;

import dalvik.system.PathClassLoader;

public class VehicleProviderTest {
    static void check(boolean x) {
        if (!x) {
            throw new AssertionError();
        }
    }

    static void reject(VinIdentityProvider p) {
        int c = Binder.clears;

        try {
            p.call("getVehicleType", null, null);
            throw new AssertionError("unauthorized accepted");
        } catch (SecurityException ok) {
            check(c == Binder.clears);
        }
    }

    public static void main(String[] a) throws Exception {
        PathClassLoader.fixture = a[0];
        Context c = new Context();
        ContentProvider.context = c;
        byte[] der = java.nio.file.Files.readAllBytes(java.nio.file.Path.of(a[1]));
        VinIdentityProvider p = new VinIdentityProvider(OemProviderTest.sha256(der));
        p.onCreate();
        Binder.uid = 10130;
        c.manager.signature = 0;
        VehicleFixtureState.value = 134;
        check(p.call("getVehicleType", null, null).getInt("vehicle_type") == 134);
        check(VehicleFixtureState.calls == 1 && Binder.clears == 1 && Binder.restores == 1);
        VehicleFixtureState.value = 135;
        check(p.call("getVehicleType", null, null).isEmpty());
        VehicleFixtureState.value = 0;
        check(p.call("getVehicleType", null, null).isEmpty());
        VehicleFixtureState.fail = true;
        check(p.call("getVehicleType", null, null).isEmpty());
        VehicleFixtureState.fail = false;
        check(Binder.clears == 4 && Binder.restores == 4);
        c.manager.packages = new String[] {"com.baidu.naviauto", "other"};
        reject(p);
        c.manager.packages = new String[] {"other"};
        reject(p);
        c.manager.packages = new String[] {"com.baidu.naviauto"};
        Binder.uid = 1000;
        reject(p);
        Binder.uid = 10130;
        c.manager.signature = -1;
        c.manager.signingInfo = null;
        reject(p);
        android.content.pm.Signature cert = new android.content.pm.Signature(der);
        android.content.pm.SigningInfo info = new android.content.pm.SigningInfo();
        info.current = new android.content.pm.Signature[] {cert};
        info.history = new android.content.pm.Signature[] {cert};
        c.manager.signingInfo = info;
        VehicleFixtureState.value = 134;
        check(
                p.call("getVehicleType", null, null).getInt("vehicle_type") == 134
                        && !Binder.cleared);
        info.multiple = true;
        reject(p);
        int n = VehicleFixtureState.calls;

        try {
            p.call("getVehicleType", "43", null);
            throw new AssertionError();
        } catch (IllegalArgumentException expected) {
        }

        check(VehicleFixtureState.calls == n);

        try {
            Class.forName("com.qinggan.vehicle.VehicleConfigHelper", false, c.getClassLoader());
            throw new AssertionError("parent polluted");
        } catch (ClassNotFoundException expected) {
        }

        System.out.println(
                "PASS fixed vehicle getter: genuine 134, 0/other/error empty, caller gates,"
                        + " identity and unmodified parent");
    }
}
