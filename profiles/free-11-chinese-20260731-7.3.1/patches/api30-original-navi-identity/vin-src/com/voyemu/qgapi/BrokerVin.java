package com.voyemu.qgapi;

import android.content.Context;
import android.net.Uri;
import android.os.Bundle;
import android.os.Process;

public final class BrokerVin {
    interface Environment {
        int uid();

        String[] packages(int uid);

        String broker() throws Exception;
    }

    static String select(String original, Environment env) {
        if (original != null && !original.isEmpty()) {
            return original;
        }

        try {
            int uid = env.uid();

            if (uid < 10000) {
                return original;
            }

            String[] pkgs = env.packages(uid);

            if (pkgs == null || pkgs.length != 1 || !"com.baidu.naviauto".equals(pkgs[0])) {
                return original;
            }

            String s = env.broker();

            return s != null && !s.isEmpty() ? s : original;
        } catch (Exception | LinkageError e) {
            // checkstyle-allow IllegalCatch: any caller or broker lookup failure retains the
            // original VIN
            return original;
        }
    }

    public static String select(String original) {
        if (original != null && !original.isEmpty()) {
            return original;
        }

        try {
            final Context context =
                    (Context)
                            Class.forName("com.voyemu.navi.OwnContext")
                                    .getMethod("get")
                                    .invoke(null);

            if (context == null) {
                return original;
            }

            return select(
                    original,
                    new Environment() {
                        @Override
                        public int uid() {
                            return Process.myUid();
                        }

                        @Override
                        public String[] packages(int uid) {
                            return context.getPackageManager().getPackagesForUid(uid);
                        }

                        @Override
                        public String broker() {
                            Bundle b =
                                    context.getContentResolver()
                                            .call(
                                                    Uri.parse(
                                                            "content://com.voyemu.free731.identity"),
                                                    "getVin",
                                                    null,
                                                    null);

                            return b == null ? null : b.getString("vin");
                        }
                    });
        } catch (Exception | LinkageError e) {
            // checkstyle-allow IllegalCatch: reflection or identity broker failure retains the
            // original VIN
            return original;
        }
    }
}
