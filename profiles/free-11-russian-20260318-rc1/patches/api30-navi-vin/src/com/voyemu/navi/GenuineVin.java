package com.voyemu.navi;

import android.content.Context;
import android.net.Uri;
import android.os.Bundle;

/**
 * Preserve OEM input; if absent, consult the original getter or the caller-bound OEM identity
 * broker.
 */
public final class GenuineVin {
    public static Object selectContent(String key, Object original) {
        if (!"vin".equals(key) || (original != null && !(original instanceof String))) {
            return original;
        }

        return select((String) original);
    }

    public static String select(String original) {
        if (original != null && !original.isEmpty()) {
            return original;
        }

        try {
            Object value =
                    Class.forName("com.qinggan.os.SystemProperties")
                            .getMethod("getVinCode")
                            .invoke(null);

            if (value instanceof String && !((String) value).isEmpty()) {
                return (String) value;
            }
        } catch (Exception | LinkageError ignored) {
            // checkstyle-allow IllegalCatch: reflection or vehicle service failure falls through to
            // the identity broker
            // An unavailable vehicle service falls through to the next VIN source.
        }

        try {
            Context context =
                    (Context)
                            Class.forName("com.baidu.mapautosdk.util.ContextHolder")
                                    .getMethod("getContext")
                                    .invoke(null);

            if (context != null) {
                Bundle result =
                        context.getContentResolver()
                                .call(
                                        Uri.parse("content://com.voyemu.free731.identity"),
                                        "getVin",
                                        null,
                                        null);
                String value = result == null ? null : result.getString("vin");

                if (value != null && !value.isEmpty()) {
                    return value;
                }
            }
        } catch (Exception | LinkageError ignored) {
            // checkstyle-allow IllegalCatch: reflection or identity broker failure retains the
            // original VIN
            // An unavailable property falls through to the next VIN source.
        }

        return original;
    }
}
