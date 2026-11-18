package com.voyemu.display;

import java.io.Closeable;
import java.io.FileDescriptor;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;

/** Exact private command extension; original shell dispatch remains the fallback. */
public final class DmsPowerCommand {
    private static final IdentityHashMap<Object, Bridge> BRIDGES = new IdentityHashMap<>();
    private static final String COMMAND = "voyemu-passion-screen";

    static final class WatchBudget {
        int bytes;

        boolean allow(String event) {
            int next = event.getBytes(java.nio.charset.StandardCharsets.UTF_8).length + 1;

            if (next > 32768 - bytes) {
                return false;
            }

            bytes += next;

            return true;
        }
    }

    private static final class Watcher {
        final String session, epoch, boot;
        final ArrayBlockingQueue<String> queue = new ArrayBlockingQueue<>(2);
        volatile boolean stop;

        Watcher(String session, String epoch, String boot) {
            this.session = session;
            this.epoch = epoch;
            this.boot = boot;
        }
    }

    private static final class Bridge {
        final DmsDisplayPowerBackend backend;
        final PassionDisplayPower power;
        final Map<Integer, String> latest = new TreeMap<>();
        final Map<Integer, Long> versions = new TreeMap<>();
        Watcher watcher;
        long sequence;

        Bridge(Object service) throws Exception {
            backend = new DmsDisplayPowerBackend(service);
            power = new PassionDisplayPower(backend);
        }

        synchronized void enqueue() {
            if (watcher == null) {
                return;
            }

            String event =
                    "{\"schema\":1,\"ok\":true,\"kind\":\"display_state_event\",\"session_id\":"
                            + quoted(watcher.session)
                            + ",\"watch_id\":"
                            + quoted(watcher.epoch)
                            + ",\"boot_id\":"
                            + quoted(watcher.boot)
                            + ",\"sequence\":"
                            + ++sequence
                            + ",\"guest_time_ms\":"
                            + System.currentTimeMillis()
                            + ",\"states\":["
                            + String.join(",", latest.values())
                            + "]}";

            if (!watcher.queue.offer(event)) {
                watcher.queue.clear();
                watcher.queue.offer(event);
            }
        }

        synchronized void applied(PassionDisplayPower.Proof p) {
            int id =
                    p.uniqueId().equals("local:8141241408256768")
                            ? 0
                            : p.uniqueId()
                                            .equals(
                                                    "virtual:com.android.emulator.multidisplay:1234562")
                                    ? 1
                                    : -1;

            if (id < 0 || !p.applied()) {
                throw new IllegalStateException("Canonical applied event required");
            }

            if (p.transaction() <= versions.getOrDefault(id, 0L)) {
                return;
            }

            versions.put(id, p.transaction());
            latest.put(
                    id,
                    "{\"display\":"
                            + id
                            + ",\"unique_id\":"
                            + quoted(p.uniqueId())
                            + ",\"state\":"
                            + p.state()
                            + ",\"kind\":\"applied\",\"request\":"
                            + p.request()
                            + ",\"transaction\":"
                            + p.transaction()
                            + ",\"observed_ms\":"
                            + p.observedMs()
                            + ",\"order\":"
                            + p.transaction()
                            + "}");
            enqueue();
        }

        synchronized void merge(List<DmsDisplayPowerBackend.Snapshot> snapshot) {
            for (DmsDisplayPowerBackend.Snapshot observed : snapshot) {
                int id = observed.device().logical();

                if (observed.order() <= versions.getOrDefault(id, 0L)) {
                    continue;
                }

                versions.put(id, observed.order());
                PassionDisplayPower.Proof proof = observed.proof();
                boolean verified =
                        proof != null && proof.applied() && proof.state() == observed.state();
                latest.put(
                        id,
                        "{\"display\":"
                                + id
                                + ",\"unique_id\":"
                                + quoted(observed.device().uniqueId())
                                + ",\"state\":"
                                + observed.state()
                                + ",\"kind\":\"observed\",\"request\":"
                                + (verified ? proof.request() : 0)
                                + ",\"transaction\":"
                                + (verified ? proof.transaction() : 0)
                                + ",\"observed_ms\":"
                                + observed.observedMs()
                                + ",\"order\":"
                                + observed.order()
                                + "}");
            }

            enqueue();
        }

        void refresh() {
            try {
                @SuppressWarnings("unchecked")
                List<DmsDisplayPowerBackend.Snapshot> snapshot =
                        (List<DmsDisplayPowerBackend.Snapshot>)
                                power.read(() -> {
                                    List<DmsDisplayPowerBackend.Snapshot> all = new ArrayList<>();

                                    for (int id = 0; id < 2; id++) {
                                        all.add(backend.snapshot(power.target(id)));
                                    }

                                    return all;
                                });
                merge(snapshot);
            } catch (RuntimeException unknown) {
                // checkstyle-allow IllegalCatch: shell/Binder boundary reports failures without
                // losing completion or original dispatch.
                synchronized (this) {
                    latest.clear();
                    enqueue();
                }
            }
        }

        void watch(FileDescriptor out, String session, String epoch, String boot) throws Exception {
            Watcher owned = new Watcher(session, epoch, boot);

            synchronized (this) {
                if (watcher != null) {
                    watcher.stop = true;
                    watcher.queue.offer("");
                }

                watcher = owned;
            }

            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(20);
            WatchBudget budget = new WatchBudget();
            String reason = "lease";

            try {
                refresh();

                while (!owned.stop && System.nanoTime() < deadline) {
                    String event = owned.queue.poll(1000, TimeUnit.MILLISECONDS);

                    if (owned.stop) {
                        break;
                    }

                    if (event != null && !event.isEmpty()) {
                        if (!budget.allow(event)) {
                            reason = "byte_budget";
                            break;
                        }

                        output(out, event, true);
                    } else {
                        refresh();
                    }
                }

                output(
                        out,
                        "{\"schema\":1,\"kind\":\"display_state_end\",\"session_id\":"
                                + quoted(session)
                                + ",\"watch_id\":"
                                + quoted(epoch)
                                + ",\"boot_id\":"
                                + quoted(boot)
                                + ",\"reason\":"
                                + quoted(reason)
                                + "}",
                        true);
            } finally {
                synchronized (this) {
                    owned.stop = true;

                    if (watcher == owned) {
                        watcher = null;
                    }
                }
            }
        }
    }

    private DmsPowerCommand() {}

    private static synchronized Bridge bridge(Object command) throws Exception {
        Object service = DmsDisplayPowerBackend.field(command, "mService");
        Bridge bridge = BRIDGES.get(service);

        if (bridge == null) {
            bridge = new Bridge(service);
            BRIDGES.put(service, bridge);
        }

        return bridge;
    }

    private static String quoted(String value) {
        return "\"" + value.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    private static String boot() throws IOException {
        try (FileInputStream input = new FileInputStream("/proc/sys/kernel/random/boot_id")) {
            byte[] value = new byte[128];
            int size = input.read(value);
            String id =
                    size < 0
                            ? ""
                            : new String(value, 0, size, java.nio.charset.StandardCharsets.US_ASCII)
                                    .trim();

            if (!id.matches("[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}")) {
                throw new IOException("Boot fence unavailable");
            }

            return id;
        }
    }

    private static void output(FileDescriptor out, String value) throws Exception {
        output(out, value, false);
    }

    // The platform class is resolved by name; a mismatch fails as before.
    @SuppressWarnings("UnsafeReflectiveConstructionCast")
    private static void output(FileDescriptor out, String value, boolean nonblocking)
            throws Exception {
        if (out == null) {
            return;
        }

        Class<?> pfd = Class.forName("android.os.ParcelFileDescriptor");
        Object duplicate = pfd.getMethod("dup", FileDescriptor.class).invoke(null, out);
        OutputStream stream = null;

        try {
            if (nonblocking) {
                FileDescriptor fd =
                        (FileDescriptor) pfd.getMethod("getFileDescriptor").invoke(duplicate);
                Class<?> os = Class.forName("android.system.Os"),
                        constants = Class.forName("android.system.OsConstants");
                Method fcntl = os.getMethod("fcntlInt", FileDescriptor.class, int.class, int.class);
                int get = constants.getField("F_GETFL").getInt(null),
                        set = constants.getField("F_SETFL").getInt(null),
                        flag = constants.getField("O_NONBLOCK").getInt(null);
                int previous = ((Number) fcntl.invoke(null, fd, get, 0)).intValue();
                fcntl.invoke(null, fd, set, previous | flag);
            }

            stream =
                    (OutputStream)
                            Class.forName("android.os.ParcelFileDescriptor$AutoCloseOutputStream")
                                    .getConstructor(pfd)
                                    .newInstance(duplicate);

            try (OutputStream owned = stream) {
                owned.write((value + "\n").getBytes(java.nio.charset.StandardCharsets.UTF_8));
                owned.flush();
            }
        } finally {
            if (stream == null) {
                ((Closeable) duplicate).close();
            }
        }
    }

    private static void result(Object receiver, int status) throws Exception {
        if (receiver != null) {
            DmsDisplayPowerBackend.call(
                    receiver,
                    "send",
                    new Class<?>[] {int.class, Class.forName("android.os.Bundle")},
                    status,
                    null);
        }
    }

    private static final class ShellCall {
        final Object binder, callback, receiver;
        final FileDescriptor in, out, err;
        final String[] args;

        ShellCall(
                Object binder,
                FileDescriptor in,
                FileDescriptor out,
                FileDescriptor err,
                String[] args,
                Object callback,
                Object receiver) {
            this.binder = binder;
            this.in = in;
            this.out = out;
            this.err = err;
            this.args = args;
            this.callback = callback;
            this.receiver = receiver;
        }
    }

    private static int original(Object command, ShellCall shell) {
        try {
            return ((Number)
                            DmsDisplayPowerBackend.call(
                                    command,
                                    "exec",
                                    new Class<?>[] {
                                        Class.forName("android.os.Binder"),
                                        FileDescriptor.class,
                                        FileDescriptor.class,
                                        FileDescriptor.class,
                                        String[].class,
                                        Class.forName("android.os.ShellCallback"),
                                        Class.forName("android.os.ResultReceiver")
                                    },
                                    shell.binder,
                                    shell.in,
                                    shell.out,
                                    shell.err,
                                    shell.args,
                                    shell.callback,
                                    shell.receiver))
                    .intValue();
        } catch (InvocationTargetException invocation) {
            Throwable cause = invocation.getCause();

            if (cause instanceof RuntimeException) {
                throw (RuntimeException) cause;
            }

            if (cause instanceof Error) {
                throw (Error) cause;
            }

            throw new IllegalStateException(cause);
        } catch (Exception failure) {
            // checkstyle-allow IllegalCatch: shell/Binder boundary reports failures without losing
            // completion or original dispatch.
            throw new IllegalStateException("Original shell dispatch unavailable", failure);
        }
    }

    private static void dispatch(Bridge bridge, int uid, String boot, ShellCall shell)
            throws Exception {
        String[] args = shell.args;
        FileDescriptor out = shell.out;

        if (args[1].equals("watch")) {
            if (uid != 0
                    || args.length != 5
                    || !args[2].matches("[0-9a-f]{32}")
                    || !args[3].matches("[0-9a-f]{32}")
                    || !args[4].equals(boot)) {
                throw new SecurityException("Exact root-owned sameboot watch required");
            }

            bridge.watch(out, args[2], args[3], boot);

            return;
        }

        int display = Integer.parseInt(args[2]);

        if (display < 0 || display > 1) {
            throw new IllegalArgumentException("Only canonical displays supported");
        }

        if (args.length == 3 && args[1].equals("query")) {
            query(bridge, boot, display, out);
        } else if (args.length == 4 && args[1].equals("set")) {
            set(bridge, uid, boot, display, args[3], out);
        } else {
            throw new IllegalArgumentException("Unsupported reserved command");
        }
    }

    private static void query(Bridge bridge, String boot, int display, FileDescriptor out)
            throws Exception {
        DmsDisplayPowerBackend.Snapshot snapshot =
                (DmsDisplayPowerBackend.Snapshot)
                        bridge.power.read(
                                () -> bridge.backend.snapshot(bridge.power.target(display)));
        PassionDisplayPower.Proof previous = snapshot.proof();
        boolean verified =
                previous != null
                        && previous.applied()
                        && previous.state() == snapshot.state()
                        && previous.uniqueId().equals(snapshot.device().uniqueId());

        output(
                out,
                "{\"ok\":true,\"kind\":\"observed\",\"boot_id\":"
                        + quoted(boot)
                        + ",\"display\":"
                        + display
                        + ",\"unique_id\":"
                        + quoted(snapshot.device().uniqueId())
                        + ",\"state\":"
                        + snapshot.state()
                        + ",\"request\":"
                        + (verified ? previous.request() : 0)
                        + ",\"transaction\":"
                        + (verified ? previous.transaction() : 0)
                        + ",\"observed_ms\":"
                        + snapshot.observedMs()
                        + ",\"order\":"
                        + snapshot.order()
                        + "}");
    }

    private static void set(
            Bridge bridge, int uid, String boot, int display, String requested, FileDescriptor out)
            throws Exception {
        int state = requested.equals("OFF") ? 1 : requested.equals("ON") ? 2 : 0;

        if (state == 0) {
            throw new IllegalArgumentException("Exact ON/OFF state required");
        }

        PassionDisplayPower.Proof proof = bridge.power.request(uid, display == 0 ? 2 : 3, state);
        bridge.applied(proof);
        output(
                out,
                "{\"ok\":true,\"kind\":\"applied\",\"boot_id\":"
                        + quoted(boot)
                        + ",\"display\":"
                        + display
                        + ",\"unique_id\":"
                        + quoted(proof.uniqueId())
                        + ",\"state\":"
                        + proof.state()
                        + ",\"request\":"
                        + proof.request()
                        + ",\"transaction\":"
                        + proof.transaction()
                        + ",\"observed_ms\":"
                        + proof.observedMs()
                        + ",\"order\":"
                        + proof.transaction()
                        + "}");
    }

    public static int exec(
            // checkstyle-allow ParameterNumber: pinned Android shell hook ABI requires eight
            // registers.
            Object command,
            Object binder,
            FileDescriptor in,
            FileDescriptor out,
            FileDescriptor err,
            String[] args,
            Object callback,
            Object receiver) {
        ShellCall shell = new ShellCall(binder, in, out, err, args, callback, receiver);

        if (args == null || args.length == 0 || !COMMAND.equals(args[0])) {
            return original(command, shell);
        }

        int status = 1;

        try {
            int uid =
                    ((Number)
                                    Class.forName("android.os.Binder")
                                            .getMethod("getCallingUid")
                                            .invoke(null))
                            .intValue();

            if (uid != 0 && uid != 1000) {
                throw new SecurityException("System caller required");
            }

            if (args.length < 3 || args.length > 5) {
                throw new IllegalArgumentException("Exact command arguments required");
            }

            Bridge bridge = bridge(command);
            String boot = boot();
            dispatch(bridge, uid, boot, shell);
            status = 0;
        } catch (Exception failure) {
            // checkstyle-allow IllegalCatch: shell/Binder boundary reports failures without losing
            // completion or original dispatch.
            try {
                output(err, "{\"ok\":false,\"error\":\"DISPLAY_POWER_UNAVAILABLE\"}");
            } catch (Exception pipeFailure) {
                // checkstyle-allow IllegalCatch: shell/Binder boundary reports failures without
                // losing completion or original dispatch.
                failure.addSuppressed(pipeFailure);
            }
        }

        try {
            result(receiver, status);
        } catch (Exception failure) {
            // checkstyle-allow IllegalCatch: shell/Binder boundary reports failures without losing
            // completion or original dispatch.
            throw new IllegalStateException("Command completion unavailable", failure);
        }

        return status;
    }
}
