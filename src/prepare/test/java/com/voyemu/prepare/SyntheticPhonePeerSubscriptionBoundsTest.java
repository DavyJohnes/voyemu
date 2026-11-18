package com.voyemu.prepare;

import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Path;

/** Supplementary tests leave the accepted nine-case fixtures and production sources unchanged. */
public final class SyntheticPhonePeerSubscriptionBoundsTest {
    public static void main(String[] args) throws Exception {
        if (args.length != 4) {
            throw new IllegalArgumentException(
                    "production classes, boundary classes, supplementary classes, SDK jar");
        }

        try (URLClassLoader boundary =
                        new URLClassLoader(
                                new URL[] {
                                    Path.of(args[2]).toUri().toURL(),
                                    Path.of(args[1]).toUri().toURL(),
                                    Path.of(args[3]).toUri().toURL()
                                },
                                ClassLoader.getPlatformClassLoader());
                URLClassLoader phone =
                        new URLClassLoader(
                                new URL[] {Path.of(args[0]).toUri().toURL()}, boundary)) {
            phone.loadClass("fixture.PhonePeerProcessProbe")
                    .getMethod("create", String.class)
                    .invoke(null, "phone");
            Class<?> runtime = boundary.loadClass("android.os.PeerTestRuntime");

            try {
                boundary.loadClass("android.os.PeerSubscriptionBoundsChecks")
                        .getMethod("run", Object.class)
                        .invoke(null, runtime.getField("phone").get(null));
            } catch (java.lang.reflect.InvocationTargetException failure) {
                Throwable cause = failure.getCause();

                if (cause instanceof Exception exception) {
                    throw exception;
                }

                if (cause instanceof Error error) {
                    throw error;
                }

                throw failure;
            }

            boundary.loadClass("android.util.Log")
                    .getMethod("assertSafe", int.class)
                    .invoke(null, 96);
        }

        System.out.println("SyntheticPhonePeerSubscriptionBoundsTest PASS");
    }
}
