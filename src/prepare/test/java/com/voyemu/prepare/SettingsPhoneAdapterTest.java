package com.voyemu.prepare;

import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Path;

/** Exercises the 6.11 adapter in the existing isolated Phone/consumer boundary. */
public final class SettingsPhoneAdapterTest {
    private static final String ADDRESS = "02:00:00:00:00:01";

    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static Object callback;
    private static Class<?> binderType;

    private static boolean command(Object adapter, String operation, String address)
            throws Exception {
        return (Boolean)
                adapter.getClass()
                        .getMethod(
                                "settingsPeer",
                                String.class,
                                String.class,
                                binderType,
                                int.class,
                                int.class)
                        .invoke(adapter, operation, address, callback, 1000, 4103);
    }

    private static String control(Class<?> probe, Object phone, String operation, String payload)
            throws Exception {
        return (String)
                probe.getMethod("control", String.class, String.class)
                        .invoke(phone, operation, payload);
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 4) {
            throw new IllegalArgumentException(
                    "6.11 compiled classes, boundary classes, 611 overlay, SDK ABI jar");
        }

        URL classes = Path.of(args[0]).toUri().toURL(), sdk = Path.of(args[3]).toUri().toURL();

        try (URLClassLoader boundary =
                        new URLClassLoader(
                                new URL[] {
                                    Path.of(args[2]).toUri().toURL(),
                                    Path.of(args[1]).toUri().toURL(),
                                    sdk
                                },
                                ClassLoader.getPlatformClassLoader());
                URLClassLoader phoneLoader = new URLClassLoader(new URL[] {classes}, boundary);
                URLClassLoader mirrorLoader = new URLClassLoader(new URL[] {classes}, boundary)) {
            Class<?> runtime = boundary.loadClass("android.os.PeerTestRuntime");
            Class<?> probe = phoneLoader.loadClass("fixture.PhonePeerProcessProbe");
            Object phone = probe.getMethod("create", String.class).invoke(null, "phone");
            Object adapter = runtime.getField("phone").get(null);
            binderType = boundary.loadClass("android.os.IBinder");
            callback =
                    boundary.loadClass("android.os.SettingsTestCallback")
                            .getConstructor()
                            .newInstance();
            adapter.getClass()
                    .getMethod("subscribePeer", binderType, int.class, int.class)
                    .invoke(adapter, callback, 1000, 4103);
            check(!command(adapter, "connect", "02:00:00:00:00:02"), "unknown device accepted");
            check(!command(adapter, "other", ADDRESS), "unknown command accepted");
            check(
                    !command(adapter, "disconnect", ADDRESS),
                    "idle disconnected peer accepted disconnect");
            check(command(adapter, "connect", ADDRESS), "idle peer did not connect");
            check(!command(adapter, "connect", ADDRESS), "duplicate connect accepted");
            Class<?> callbackType =
                    phoneLoader.loadClass("com.qinggan.bluetooth.profile.IHfpCallback");
            Object hfp =
                    adapter.getClass()
                            .getMethod("getHfp", callbackType)
                            .invoke(adapter, new Object[] {null});
            java.lang.reflect.Method dial = hfp.getClass().getDeclaredMethod("dial", String.class);
            dial.setAccessible(true);
            dial.invoke(hfp, "+70000000001");
            boundary.loadClass("android.os.Handler").getMethod("drain").invoke(null);
            check(!command(adapter, "disconnect", ADDRESS), "dialing call allowed disconnect");
            control(probe, phone, "hangup", "{}");
            control(probe, phone, "incoming", "{\"number\":\"+70000000001\"}");
            check(!command(adapter, "disconnect", ADDRESS), "incoming call allowed disconnect");
            control(probe, phone, "answer", "{}");
            check(!command(adapter, "disconnect", ADDRESS), "active call allowed disconnect");
            control(probe, phone, "hangup", "{}");
            check(
                    command(adapter, "disconnect", ADDRESS),
                    "terminated call did not allow disconnect");
            check(!command(adapter, "disconnect", ADDRESS), "duplicate disconnect accepted");
            Class<?> mirrorProbe = mirrorLoader.loadClass("fixture.PhonePeerProcessProbe");
            Object mirror = mirrorProbe.getMethod("create", String.class).invoke(null, "launcher");
            java.lang.reflect.Field field = mirrorProbe.getDeclaredField("adapter");
            field.setAccessible(true);
            check(!command(field.get(mirror), "connect", ADDRESS), "non-authority changed peer");
        }

        System.out.println("SettingsPhoneAdapterTest PASS");
    }
}
