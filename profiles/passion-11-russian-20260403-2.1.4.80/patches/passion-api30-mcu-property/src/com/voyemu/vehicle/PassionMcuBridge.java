package com.voyemu.vehicle;

import android.content.Context;
import android.os.Process;

import com.mega.car.hal.IProperty;

import java.io.FileInputStream;
import java.nio.charset.StandardCharsets;

/** Runs inside the original Monza APK classloader and system-UID process. */
public final class PassionMcuBridge {
    private static PassionMcuProperty bridge;
    private static String boot;

    public static synchronized String boot() {
        if (boot == null) {
            try (FileInputStream file = new FileInputStream("/proc/sys/kernel/random/boot_id")) {
                byte[] bytes = new byte[128];
                int count = file.read(bytes);

                if (count < 36 || count >= bytes.length || file.read() != -1) {
                    throw new IllegalStateException("boot extent");
                }

                String actual = new String(bytes, 0, count, StandardCharsets.US_ASCII).trim();

                if (!actual.matches("[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}")) {
                    throw new IllegalStateException("boot format");
                }

                boot = actual;
            } catch (java.io.IOException error) {
                throw new IllegalStateException("PASSION_MCU: actual boot unavailable", error);
            }
        }

        return boot;
    }

    public static synchronized IProperty wrap(Context context, IProperty original) {
        if (context == null
                || !"com.mega.car".equals(context.getPackageName())
                || Process.myUid() != 1000
                || original == null
                || !original.getClass().getName().equals("com.mega.car.MonzaPropertyAdapter")
                || original.getClass().getClassLoader() != PassionMcuBridge.class.getClassLoader()
                || bridge != null) {
            throw new IllegalStateException("PASSION_MCU: exact original Monza owner required");
        }

        bridge = new PassionMcuProperty(original, boot());

        return bridge;
    }

    public static synchronized PassionMcuProperty current() {
        return bridge;
    }
}
