package com.voyemu.prepare;

import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Path;

public final class SyntheticPhonePeerPbapRaceTest {
    public static void main(String[] args) throws Exception {
        if (args.length != 5
                || !java.util.Set.of("history", "direct", "cancel", "reconnect", "timeout")
                        .contains(args[4])) {
            throw new IllegalArgumentException(
                    "production classes, boundary classes, app classes, SDK jar, scenario:"
                            + " history|direct|cancel|reconnect|timeout");
        }

        try (URLClassLoader boundary =
                        new URLClassLoader(
                                new URL[] {
                                    Path.of(args[1]).toUri().toURL(),
                                    Path.of(args[3]).toUri().toURL()
                                },
                                ClassLoader.getPlatformClassLoader());
                URLClassLoader own =
                        new URLClassLoader(new URL[] {Path.of(args[0]).toUri().toURL()}, boundary);
                URLClassLoader app =
                        new URLClassLoader(
                                new URL[] {Path.of(args[2]).toUri().toURL()}, boundary)) {
            own.loadClass("fixture.PhonePbapRaceProbe")
                    .getMethod("run", String.class, ClassLoader.class)
                    .invoke(null, args[4], app);
        }

        System.out.println("SyntheticPhonePeerPbapRaceTest PASS " + args[4]);
    }
}
