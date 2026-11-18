package com.voyemu.prepare;

import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Path;

public final class SyntheticPhonePeerSubscriberNumberTest {
    public static void main(String[] args) throws Exception {
        if (args.length != 4) {
            throw new IllegalArgumentException("production,boundary,test classes,SDK");
        }

        try (URLClassLoader boundary =
                        new URLClassLoader(
                                new URL[] {
                                    Path.of(args[1]).toUri().toURL(),
                                    Path.of(args[3]).toUri().toURL()
                                },
                                ClassLoader.getPlatformClassLoader());
                URLClassLoader phone =
                        new URLClassLoader(
                                new URL[] {
                                    Path.of(args[2]).toUri().toURL(),
                                    Path.of(args[0]).toUri().toURL()
                                },
                                boundary)) {
            try {
                phone.loadClass("fixture.SubscriberNumberContractProbe")
                        .getMethod("run")
                        .invoke(null);
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
        }

        System.out.println("SyntheticPhonePeerSubscriberNumberTest PASS");
    }
}
