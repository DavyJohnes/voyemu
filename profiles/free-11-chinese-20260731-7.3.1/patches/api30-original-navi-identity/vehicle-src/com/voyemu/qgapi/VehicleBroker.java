package com.voyemu.qgapi;

import android.content.Context;
import android.net.Uri;
import android.os.Bundle;
import android.os.Process;

public final class VehicleBroker {
    interface Environment {
        int uid();

        String[] packages(int uid);

        int broker() throws Exception;
    }

    static int lookup(Environment e) {
        try {
            int uid = e.uid();

            if (uid < 10000) {
                return 0;
            }

            String[] p = e.packages(uid);

            if (p == null || p.length != 1 || !"com.baidu.naviauto".equals(p[0])) {
                return 0;
            }

            int x = e.broker();

            return x == 134 ? x : 0;
        } catch (Exception | LinkageError x) {
            // checkstyle-allow IllegalCatch: any identity lookup failure rejects the vehicle type
            return 0;
        }
    }

    public static int lookup() {
        try {
            final Context c =
                    (Context)
                            Class.forName("com.voyemu.navi.OwnContext")
                                    .getMethod("get")
                                    .invoke(null);

            if (c == null) {
                return 0;
            }

            return lookup(
                    new Environment() {
                        @Override
                        public int uid() {
                            return Process.myUid();
                        }

                        @Override
                        public String[] packages(int uid) {
                            return c.getPackageManager().getPackagesForUid(uid);
                        }

                        @Override
                        public int broker() {
                            Bundle b =
                                    c.getContentResolver()
                                            .call(
                                                    Uri.parse(
                                                            "content://com.voyemu.free731.identity"),
                                                    "getVehicleType",
                                                    null,
                                                    null);

                            return b == null ? 0 : b.getInt("vehicle_type", 0);
                        }
                    });
        } catch (Exception | LinkageError e) {
            // checkstyle-allow IllegalCatch: reflection or identity broker failure rejects the
            // vehicle type
            return 0;
        }
    }
}
