package com.voyemu.prepare;

import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Path;
import java.util.concurrent.atomic.AtomicInteger;

/** Settings mirror reaches Phone through the private capability; no OEM objects cross loaders. */
public final class SettingsPhoneCapabilityTest {
    static void check(boolean b, String message) {
        if (!b) {
            throw new AssertionError(message);
        }
    }

    static void drain(Class<?> handler) throws Exception {
        handler.getMethod("drain").invoke(null);
    }

    static void await(java.util.concurrent.Callable<Boolean> condition, Class<?> handler)
            throws Exception {
        long end = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(5);

        while (System.nanoTime() < end) {
            drain(handler);

            if (condition.call()) {
                return;
            }

            Thread.sleep(10);
        }

        throw new AssertionError("timed out waiting for authoritative state");
    }

    /**
     * Phone publishes the new state before the Settings worker journals the command as idle and
     * releases its fence, so the next command waits for that fence instead of racing the journal.
     */
    static void awaitCommandIdle(Object adapter, Class<?> handler) throws Exception {
        java.lang.reflect.Field field = adapter.getClass().getDeclaredField("subscription");

        field.setAccessible(true);
        Object subscription = field.get(adapter);
        java.lang.reflect.Field pending =
                subscription.getClass().getDeclaredField("commandPending");
        java.lang.reflect.Field unknown =
                subscription.getClass().getDeclaredField("commandUnknown");
        pending.setAccessible(true);
        unknown.setAccessible(true);
        await(
                () ->
                        !((java.util.concurrent.atomic.AtomicBoolean) pending.get(subscription))
                                        .get()
                                && !unknown.getBoolean(subscription),
                handler);
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 4 && args.length != 5) {
            throw new IllegalArgumentException(
                    "611 compiled classes, boundary classes, 611 boundary overlay, SDK jar"
                            + " [death]");
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
                        new URLClassLoader(new URL[] {Path.of(args[0]).toUri().toURL()}, boundary);
                URLClassLoader settings =
                        new URLClassLoader(
                                new URL[] {Path.of(args[0]).toUri().toURL()}, boundary)) {
            Class<?> runtime = boundary.loadClass("android.os.PeerTestRuntime"),
                    handler = boundary.loadClass("android.os.Handler");
            Class<?> phoneProbe = phone.loadClass("fixture.PhonePeerProcessProbe"),
                    settingsProbe = settings.loadClass("fixture.PhonePeerProcessProbe");
            phoneProbe.getMethod("create", String.class).invoke(null, "phone");

            if (args.length == 5 && args[4].startsWith("corrupt-")) {
                writeCorruptJournal(args);
            }

            Object mirror =
                    settingsProbe.getMethod("create", String.class).invoke(null, "settings");
            java.lang.reflect.Field af = settingsProbe.getDeclaredField("adapter");
            af.setAccessible(true);
            Object adapter = af.get(mirror);
            Object device =
                    adapter.getClass()
                            .getMethod("getRemoteDevice", String.class)
                            .invoke(adapter, "02:00:00:00:00:01");
            Class<?> deviceType = settings.loadClass("com.qinggan.bluetooth.profile.BTDevice");
            AtomicInteger count = (AtomicInteger) runtime.getField("COMMAND_COUNT").get(null);
            runtime.getMethod("enter", String.class).invoke(null, "settings");
            java.lang.reflect.Method connect = adapter.getClass().getMethod("connect", deviceType),
                    disconnect = adapter.getClass().getMethod("disconnect", deviceType);
            java.lang.reflect.Field incarnation =
                    adapter.getClass().getDeclaredField("remoteIncarnation");
            incarnation.setAccessible(true);
            await(() -> incarnation.get(adapter) != null, handler);

            if (args.length == 5 && args[4].startsWith("corrupt-")) {
                checkCorruptFence(adapter, connect, device, count);

                return;
            }

            check(!(Boolean) connect.invoke(adapter, device), "queued request claimed success");
            await(() -> count.get() == 1, handler);
            await(
                    () -> Boolean.TRUE.equals(settingsProbe.getMethod("connected").invoke(mirror)),
                    handler);
            awaitCommandIdle(adapter, handler);
            check(!(Boolean) disconnect.invoke(adapter, device), "queued request claimed success");
            await(() -> count.get() == 2, handler);
            await(
                    () -> Boolean.FALSE.equals(settingsProbe.getMethod("connected").invoke(mirror)),
                    handler);
            awaitCommandIdle(adapter, handler);

            if (args.length == 5 && "idle-restart".equals(args[4])) {
                checkIdleRestart(args, boundary, runtime, handler, count);

                return;
            }

            if (args.length == 5) {
                checkAuthorityDeath(args, runtime, handler, adapter, connect, device, count);

                return;
            }

            java.lang.reflect.Field subscriptionField =
                    adapter.getClass().getDeclaredField("subscription");
            subscriptionField.setAccessible(true);
            Object subscription = subscriptionField.get(adapter);
            java.lang.reflect.Field epochField =
                    subscription.getClass().getDeclaredField("clientEpoch");
            epochField.setAccessible(true);
            var clientEpoch = (java.util.concurrent.atomic.AtomicLong) epochField.get(subscription);
            long epoch = clientEpoch.get();
            int staleCount = count.get();
            clientEpoch.set(epoch + 1);
            check(!(Boolean) connect.invoke(adapter, device), "stale capability accepted command");
            Thread.sleep(50);
            check(count.get() == staleCount, "stale capability sent command");
            clientEpoch.set(epoch);
            runtime.getField("dropSettingsReply").setBoolean(null, true);
            check(!(Boolean) connect.invoke(adapter, device), "queued request claimed success");
            await(() -> count.get() == 3, handler);
            await(
                    () -> Boolean.TRUE.equals(settingsProbe.getMethod("connected").invoke(mirror)),
                    handler);
            Thread.sleep(100);
            check(!(Boolean) disconnect.invoke(adapter, device), "unknown result claimed success");
            Thread.sleep(100);
            check(count.get() == 3, "unknown result was retried");
            checkRestartedUnknown(args, boundary, runtime, handler, count);
            runtime.getMethod("killAuthority").invoke(null);
            drain(handler);
            int before = count.get();
            check(!(Boolean) connect.invoke(adapter, device), "dead capability accepted command");
            Thread.sleep(50);
            drain(handler);
            check(count.get() == before, "dead capability sent command");
        }

        System.out.println("SettingsPhoneCapabilityTest PASS");
    }

    static void writeCorruptJournal(String[] args) throws Exception {
        Path dir = Path.of(System.getProperty("phone.peer.test.files"), "com.qinggan.app.setting");

        java.nio.file.Files.createDirectories(dir);
        Path journal = dir.resolve("voyemu-settings-command.state");
        String mode = args[4].substring(8);

        if ("directory".equals(mode)) {
            java.nio.file.Files.createDirectories(journal);
        } else {
            String value =
                    "empty".equals(mode)
                            ? ""
                            : "short".equals(mode)
                                    ? "idle"
                                    : "extra".equals(mode)
                                            ? "idle\nx"
                                            : "oversize".equals(mode)
                                                    ? "x".repeat(1000)
                                                    : "pending".equals(mode) ? "pending\n" : null;

            if (value == null) {
                throw new IllegalArgumentException(mode);
            }

            java.nio.file.Files.write(
                    journal, value.getBytes(java.nio.charset.StandardCharsets.US_ASCII));
        }
    }

    static void checkCorruptFence(
            Object adapter, java.lang.reflect.Method connect, Object device, AtomicInteger count)
            throws Exception {
        java.lang.reflect.Field sf = adapter.getClass().getDeclaredField("subscription");

        sf.setAccessible(true);
        Object sub = sf.get(adapter);
        java.lang.reflect.Field unknown = sub.getClass().getDeclaredField("commandUnknown");
        unknown.setAccessible(true);
        check(unknown.getBoolean(sub), "corrupt journal did not fence");
        check(!(Boolean) connect.invoke(adapter, device), "corrupt journal claimed success");
        Thread.sleep(200);
        check(count.get() == 0, "corrupt journal sent command");
        System.out.println("SettingsPhoneCapabilityTest PASS corrupt");
    }

    static void checkIdleRestart(
            String[] args,
            URLClassLoader boundary,
            Class<?> runtime,
            Class<?> handler,
            AtomicInteger count)
            throws Exception {
        Path journal =
                Path.of(
                        System.getProperty("phone.peer.test.files"),
                        "com.qinggan.app.setting",
                        "voyemu-settings-command.state");

        await(
                () ->
                        java.nio.file.Files.exists(journal)
                                && "idle\n".equals(java.nio.file.Files.readString(journal)),
                handler);

        try (URLClassLoader restarted =
                new URLClassLoader(new URL[] {Path.of(args[0]).toUri().toURL()}, boundary)) {
            Class<?> rp = restarted.loadClass("fixture.PhonePeerProcessProbe");
            Object next = rp.getMethod("create", String.class).invoke(null, "settings");
            java.lang.reflect.Field af2 = rp.getDeclaredField("adapter");
            af2.setAccessible(true);
            Object adapter2 = af2.get(next);
            java.lang.reflect.Field inc = adapter2.getClass().getDeclaredField("remoteIncarnation");
            inc.setAccessible(true);
            await(() -> inc.get(adapter2) != null, handler);
            Object device2 =
                    adapter2.getClass()
                            .getMethod("getRemoteDevice", String.class)
                            .invoke(adapter2, "02:00:00:00:00:01");
            Class<?> type2 = restarted.loadClass("com.qinggan.bluetooth.profile.BTDevice");
            runtime.getMethod("enter", String.class).invoke(null, "settings");
            check(
                    !(Boolean)
                            adapter2.getClass()
                                    .getMethod("connect", type2)
                                    .invoke(adapter2, device2),
                    "async connect claimed success");
            await(() -> count.get() == 3, handler);
            await(() -> Boolean.TRUE.equals(rp.getMethod("connected").invoke(next)), handler);
        }

        System.out.println("SettingsPhoneCapabilityTest PASS idle-restart");
    }

    static void checkAuthorityDeath(
            String[] args,
            Class<?> runtime,
            Class<?> handler,
            Object adapter,
            java.lang.reflect.Method connect,
            Object device,
            AtomicInteger count)
            throws Exception {
        check("death".equals(args[4]), "unknown scenario");
        runtime.getMethod("killAuthority").invoke(null);
        drain(handler);
        int before = count.get();
        check(!(Boolean) connect.invoke(adapter, device), "dead capability claimed success");
        Thread.sleep(100);
        check(count.get() == before, "dead capability sent command");
        System.out.println("SettingsPhoneCapabilityTest PASS death");
    }

    static void checkRestartedUnknown(
            String[] args,
            URLClassLoader boundary,
            Class<?> runtime,
            Class<?> handler,
            AtomicInteger count)
            throws Exception {
        try (URLClassLoader restarted =
                new URLClassLoader(new URL[] {Path.of(args[0]).toUri().toURL()}, boundary)) {
            Class<?> restartedProbe = restarted.loadClass("fixture.PhonePeerProcessProbe");
            Object fresh =
                    restartedProbe.getMethod("create", String.class).invoke(null, "settings");
            java.lang.reflect.Field freshField = restartedProbe.getDeclaredField("adapter");
            freshField.setAccessible(true);
            Object freshAdapter = freshField.get(fresh);
            java.lang.reflect.Field freshInc =
                    freshAdapter.getClass().getDeclaredField("remoteIncarnation");
            freshInc.setAccessible(true);
            await(() -> freshInc.get(freshAdapter) != null, handler);
            await(
                    () -> Boolean.TRUE.equals(restartedProbe.getMethod("connected").invoke(fresh)),
                    handler);
            Object freshDevice =
                    freshAdapter
                            .getClass()
                            .getMethod("getRemoteDevice", String.class)
                            .invoke(freshAdapter, "02:00:00:00:00:01");
            Class<?> freshType = restarted.loadClass("com.qinggan.bluetooth.profile.BTDevice");
            runtime.getMethod("enter", String.class).invoke(null, "settings");
            check(
                    !(Boolean)
                            freshAdapter
                                    .getClass()
                                    .getMethod("disconnect", freshType)
                                    .invoke(freshAdapter, freshDevice),
                    "restarted unknown claimed success");
            Thread.sleep(100);
            check(count.get() == 3, "restarted process retried unknown");
        }
    }
}
