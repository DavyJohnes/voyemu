package com.voyemu.qgapi;

import android.content.ContentResolver;
import android.content.Context;
import android.net.Uri;
import android.os.Bundle;
import android.os.Process;

import com.voyemu.navi.OwnContext;

public final class EarlyContextTest {
    static void check(boolean ok, String msg) {
        if (!ok) {
            throw new AssertionError(msg);
        }
    }

    static final class Resolver extends ContentResolver {
        int calls;
        boolean fail;

        @Override
        public Bundle call(Uri u, String method, String arg, Bundle extras) {
            calls++;

            if (fail) {
                throw new IllegalStateException();
            }

            Bundle b = new Bundle();

            if (method.equals("getVin")) {
                b.putString("vin", "genuine");
            } else if (method.equals("getVehicleType")) {
                b.putInt("vehicle_type", 134);
            }

            return b;
        }
    }

    static final class Early extends Context {
        final Resolver resolver = new Resolver();

        @Override
        public ContentResolver getContentResolver() {
            return resolver;
        }
    }

    public static void main(String[] args) {
        Early c = new Early();

        Process.uid = 10130;
        OwnContext.value = c;
        OwnContext.calls = 0;
        check(
                "OEM".equals(BrokerVin.select("OEM")) && OwnContext.calls == 0,
                "preserve OEM without context lookup");
        check("genuine".equals(BrokerVin.select(null)), "early VIN context");
        check(VehicleBroker.lookup() == 134, "early vehicle context");
        check(c.resolver.calls == 2, "broker calls");
        OwnContext.value = null;
        check(
                BrokerVin.select(null) == null && VehicleBroker.lookup() == 0,
                "missing context fails closed");
        OwnContext.value = c;
        Process.uid = 1000;
        int calls = c.resolver.calls;
        check(
                BrokerVin.select(null) == null
                        && VehicleBroker.lookup() == 0
                        && c.resolver.calls == calls,
                "system uid never calls broker");
        Process.uid = 10130;
        c.manager.packages = new String[] {"com.baidu.naviauto", "other"};
        check(
                BrokerVin.select(null) == null
                        && VehicleBroker.lookup() == 0
                        && c.resolver.calls == calls,
                "shared UID fails closed");
        c.manager.packages = new String[] {"com.baidu.naviauto"};
        c.resolver.fail = true;
        check(
                BrokerVin.select(null) == null && VehicleBroker.lookup() == 0,
                "broker error preserves original");
        System.out.println("PASS early own context broker path and fail closed");
    }
}
