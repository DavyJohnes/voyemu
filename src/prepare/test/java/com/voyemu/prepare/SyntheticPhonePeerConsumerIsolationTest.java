package com.voyemu.prepare;

import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Path;
import java.util.Map;
import java.util.concurrent.TimeUnit;

/** Regression: an OEM consumer exception after commit must not become a wire/schema rejection. */
public final class SyntheticPhonePeerConsumerIsolationTest {
    public static void main(String[] args) throws Exception {
        if (args.length != 5) {
            throw new IllegalArgumentException("production, boundary, test classes, SDK, stage");
        }

        String stage = args[4];

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
                    lp = launcher.loadClass("fixture.PeerConsumerFailureProbe");
            Object authority = pp.getMethod("create", String.class).invoke(null, "phone");
            pp.getMethod("control", String.class, String.class).invoke(authority, "connect", "{}");
            Object consumer = lp.getMethod("create", String.class).invoke(null, stage);

            if ("hfpcall".equals(stage)) {
                long until = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);

                while (!Boolean.TRUE.equals(inspect(lp, consumer).get("connected"))
                        && System.nanoTime() < until) {
                    Thread.sleep(2);
                }

                pp.getMethod("control", String.class, String.class)
                        .invoke(authority, "incoming", "{\"number\":\"+70000000001\"}");
            }

            Map<String, Object> result = awaitFailure(lp, consumer);
            Map<String, Object> diagnostics = Json.object(result.get("diagnostics")),
                    events = Json.object(diagnostics.get("events"));
            System.out.println(
                    "stage="
                            + stage
                            + " committed="
                            + result.get("test_committed")
                            + " consumer_failures="
                            + result.get("test_failed")
                            + " connected="
                            + result.get("connected")
                            + " mirror_reject="
                            + events.get("MIRROR_REJECT")
                            + " healthy_connections="
                            + result.get("test_healthy_connections")
                            + " healthy_calls="
                            + result.get("test_healthy_calls"));

            if (!Boolean.TRUE.equals(result.get("test_committed"))
                    || ((Number) result.get("test_failed")).intValue() < 1) {
                throw new AssertionError("consumer failure after valid state commit prerequisite");
            }

            if (!Boolean.TRUE.equals(result.get("connected"))
                    || ((Number) events.get("MIRROR_REJECT")).intValue() != 0) {
                throw new AssertionError(
                        "valid committed mirror revoked after OEM consumer RuntimeException");
            }

            checkConsumerStage(stage, result, diagnostics, events);
            lp.getMethod("unregister").invoke(consumer);
            checkDetached(lp, consumer);
            boundary.loadClass("android.util.Log")
                    .getMethod("assertSafe", int.class)
                    .invoke(null, 192);
        }

        System.out.println("SyntheticPhonePeerConsumerIsolationTest PASS " + stage);
    }

    private static Map<String, Object> awaitFailure(Class<?> lp, Object consumer) throws Exception {
        Map<String, Object> result = null;
        long until = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);

        do {
            result = inspect(lp, consumer);

            if (((Number) result.get("test_failed")).intValue() > 0) {
                break;
            }

            Thread.sleep(2);
        } while (System.nanoTime() < until);

        return result;
    }

    private static void checkConsumerStage(
            String stage,
            Map<String, Object> result,
            Map<String, Object> diagnostics,
            Map<String, Object> events) {
        String expectedStage =
                "hfpcall".equals(stage)
                        ? "HFP_CALL"
                        : ("ibtconnection".equals(stage) ? "IBT_CONNECTION" : "HFP_CONNECTION");

        if (!expectedStage.equals(diagnostics.get("consumer_stage"))
                || !"UNSUPPORTED".equals(diagnostics.get("consumer_kind"))
                || ((Number) events.get("CONSUMER_" + expectedStage + "_FAILED")).intValue() < 1) {
            throw new AssertionError("bounded consumer stage/kind diagnostic missing");
        }

        String count =
                "hfpcall".equals(stage)
                        ? "test_healthy_calls"
                        : ("ibtconnection".equals(stage)
                                ? "test_healthy_ibt_connections"
                                : "test_healthy_connections");

        if (((Number) result.get(count)).intValue() < 1) {
            throw new AssertionError("healthy OEM listener skipped after faulty listener");
        }
    }

    private static void checkDetached(Class<?> lp, Object consumer) throws Exception {
        Map<String, Object> result, diagnostics;
        long until = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);

        do {
            result = inspect(lp, consumer);
            diagnostics = Json.object(result.get("diagnostics"));

            if (Boolean.FALSE.equals(diagnostics.get("wanted"))
                    && Boolean.FALSE.equals(diagnostics.get("bound"))
                    && Boolean.FALSE.equals(result.get("connected"))) {
                break;
            }

            Thread.sleep(2);
        } while (System.nanoTime() < until);

        if (!Boolean.FALSE.equals(diagnostics.get("wanted"))
                || !Boolean.FALSE.equals(diagnostics.get("bound"))
                || !Boolean.FALSE.equals(result.get("connected"))) {
            throw new AssertionError("last listener unregister must detach and clear mirror");
        }
    }

    private static Map<String, Object> inspect(Class<?> type, Object probe) throws Exception {
        return Json.object(Json.parse((String) type.getMethod("inspect").invoke(probe)));
    }
}
