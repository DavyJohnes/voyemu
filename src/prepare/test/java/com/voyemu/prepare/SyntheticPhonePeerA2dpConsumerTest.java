package com.voyemu.prepare;

import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Path;
import java.util.Map;
import java.util.concurrent.TimeUnit;

public final class SyntheticPhonePeerA2dpConsumerTest {
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
                        new URLClassLoader(new URL[] {Path.of(args[0]).toUri().toURL()}, boundary);
                URLClassLoader launcher =
                        new URLClassLoader(
                                new URL[] {
                                    Path.of(args[2]).toUri().toURL(),
                                    Path.of(args[0]).toUri().toURL()
                                },
                                boundary)) {
            Class<?> pp = phone.loadClass("fixture.PhonePeerProcessProbe"),
                    lp = launcher.loadClass("fixture.A2dpOriginalConsumerProbe");
            Object authority = pp.getMethod("create", String.class).invoke(null, "phone");
            pp.getMethod("control", String.class, String.class).invoke(authority, "connect", "{}");
            Object consumer = lp.getMethod("create").invoke(null);
            Map<String, Object> state = null;
            long until = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);

            do {
                state = Json.object(Json.parse((String) lp.getMethod("inspect").invoke(consumer)));

                if (((Number) state.get("test_healthy")).intValue() > 0) {
                    break;
                }

                Thread.sleep(2);
            } while (System.nanoTime() < until);

            System.out.println(
                    "connected="
                            + state.get("connected")
                            + " original_callback_completed="
                            + state.get("test_completed")
                            + " healthy_ibt="
                            + state.get("test_healthy")
                            + " a2dp_state="
                            + state.get("test_a2dp_state")
                            + " failure="
                            + state.get("test_failure"));
            lp.getMethod("printFailure").invoke(consumer);

            if (!"none".equals(state.get("test_failure"))) {
                throw new AssertionError(
                        "original A2dpManager-shaped connection callback dereferences null A2dp");
            }

            if (!Boolean.TRUE.equals(state.get("connected"))
                    || ((Number) state.get("test_completed")).intValue() != 1
                    || ((Number) state.get("test_healthy")).intValue() != 1
                    || ((Number) state.get("test_a2dp_state")).intValue() != 19) {
                throw new AssertionError(
                        "healthy original connection consumers or disconnected A2dp contract");
            }

            lp.getMethod("inactiveContract").invoke(consumer);
        }

        System.out.println("SyntheticPhonePeerA2dpConsumerTest PASS");
    }
}
