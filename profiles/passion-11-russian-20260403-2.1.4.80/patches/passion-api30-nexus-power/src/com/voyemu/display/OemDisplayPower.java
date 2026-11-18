package com.voyemu.display;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Objects;
import java.util.Stack;
import java.util.concurrent.TimeUnit;

/** Canonical OEM sender/reverse boundary. Other screens keep the original QNX path. */
public final class OemDisplayPower {
    static final String OWNER = "com.mega.nexus.server.ScreenStateProcessor";

    static final class Observation {
        final long request, transaction, observedMs, order;

        Observation(long request, long transaction, long observedMs, long order) {
            this.request = request;
            this.transaction = transaction;
            this.observedMs = observedMs;
            this.order = order;
        }
    }

    static final class Reply {
        final String boot;
        final int display;
        final String unique;
        final int state;
        final String kind;
        final long request;
        final long transaction;
        final long observedMs;
        final long order;

        Reply(
                String boot,
                int display,
                String unique,
                int state,
                String kind,
                Observation observation) {
            this.boot = boot;
            this.display = display;
            this.unique = unique;
            this.state = state;
            this.kind = kind;
            this.request = observation.request;
            this.transaction = observation.transaction;
            this.observedMs = observation.observedMs;
            this.order = observation.order;
        }

        public String boot() {
            return boot;
        }

        public int display() {
            return display;
        }

        public String unique() {
            return unique;
        }

        public int state() {
            return state;
        }

        public String kind() {
            return kind;
        }

        public long request() {
            return request;
        }

        public long transaction() {
            return transaction;
        }

        public long observedMs() {
            return observedMs;
        }

        public long order() {
            return order;
        }

        @Override
        public boolean equals(Object object) {
            if (this == object) {
                return true;
            }

            if (!(object instanceof Reply)) {
                return false;
            }

            Reply other = (Reply) object;

            return Objects.equals(boot, other.boot)
                    && display == other.display
                    && Objects.equals(unique, other.unique)
                    && state == other.state
                    && Objects.equals(kind, other.kind)
                    && request == other.request
                    && transaction == other.transaction
                    && observedMs == other.observedMs
                    && order == other.order;
        }

        @Override
        public int hashCode() {
            return Objects.hash(
                    boot, display, unique, state, kind, request, transaction, observedMs, order);
        }
    }

    interface Platform {
        String boot() throws Exception;

        Reply query(int display) throws Exception;

        Reply set(int display, boolean on) throws Exception;
    }

    private static final Engine ENGINE = new Engine(new Client());

    private OemDisplayPower() {}

    /**
     * Root app_process client in this same canonical APK: wake through genuine OEM Binder usage.
     */
    public static void main(String[] args) {
        try {
            if (args.length != 3
                    || !args[0].equals("wake")
                    || !args[2].matches("[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}")) {
                throw new IllegalArgumentException("Exact wake arguments required");
            }

            int display = Integer.parseInt(args[1]);

            if (display < 0 || display > 1) {
                throw new IllegalArgumentException("Canonical screen required");
            }

            Client platform = new Client();

            if (!platform.boot().equals(args[2])) {
                throw new IllegalStateException("Stale guest boot");
            }

            Reply before = platform.query(display);
            validate(before, args[2], display, false, false);
            Class<?> manager = Class.forName("com.mega.nexus.os.MegaScreenManager"),
                    listener = Class.forName("com.mega.nexus.os.IScreenRequestListener");
            Object instance = manager.getMethod("getInstance").invoke(null);
            manager.getMethod("requestScreenOn", int.class, int.class, listener)
                    .invoke(instance, display == 0 ? 32 : 33, display, null);
            Reply after = platform.query(display);
            validate(after, args[2], display, false, false);

            if (after.state != 2
                    || (before.state == 1
                            && (after.transaction <= before.transaction || after.request <= 0))) {
                throw new IllegalStateException("Original wake not observed");
            }

            System.out.println(
                    "{\"ok\":true,\"kind\":\"wake_observed\",\"boot_id\":\""
                            + args[2]
                            + "\",\"display\":"
                            + display
                            + ",\"unique_id\":\""
                            + after.unique
                            + "\",\"state\":"
                            + after.state
                            + ",\"request\":"
                            + after.request
                            + ",\"transaction\":"
                            + after.transaction
                            + ",\"observed_ms\":"
                            + after.observedMs
                            + ",\"order\":"
                            + after.order
                            + "}");
        } catch (Exception failure) {
            // checkstyle-allow IllegalCatch: OEM command boundary must reject failed observation
            // and restore original screen state.
            System.err.println("{\"ok\":false,\"error\":\"ORIGINAL_WAKE_UNAVAILABLE\"}");
            System.exit(1);
        }
    }

    public static void before(Object processor) {
        ENGINE.before(processor);
    }

    public static void bootstrap(Object processor) {
        ENGINE.bootstrap(processor);
    }

    public static boolean send(Object processor, boolean on) {
        return ENGINE.send(processor, on);
    }

    public static void result(Object processor, Object listener, int result) throws Exception {
        ENGINE.result(processor, listener, result);
    }

    public static void finish(Object processor) {
        ENGINE.finish(processor);
    }

    static Field field(Object p, String name) throws Exception {
        Field f = p.getClass().getDeclaredField(name);

        f.setAccessible(true);

        return f;
    }

    static Object get(Object p, String name) throws Exception {
        return field(p, name).get(p);
    }

    static Object call(Object p, String name, Class<?>[] types, Object... args) throws Exception {
        Method m = p.getClass().getDeclaredMethod(name, types);

        m.setAccessible(true);

        try {
            return m.invoke(p, args);
        } catch (InvocationTargetException e) {
            if (e.getCause() instanceof Exception) {
                throw (Exception) e.getCause();
            }

            throw e;
        }
    }

    static int canonical(Object p) throws Exception {
        if (p == null || !p.getClass().getName().equals(OWNER)) {
            return -1;
        }

        int id = ((Number) get(p, "mScreenId")).intValue();

        return id == 0 || id == 1 ? id : -1;
    }

    static String identity(int id) {
        return id == 0
                ? "local:8141241408256768"
                : "virtual:com.android.emulator.multidisplay:1234562";
    }

    static void validate(Reply r, String boot, int display, boolean applied, boolean on) {
        if (r == null
                || !r.boot.equals(boot)
                || r.display != display
                || !r.unique.equals(identity(display))
                || (r.state != 1 && r.state != 2)
                || (!r.kind.equals("observed") && !r.kind.equals("applied"))
                || r.observedMs <= 0
                || r.order <= 0
                || (applied
                        && (!r.kind.equals("applied")
                                || r.request <= 0
                                || r.transaction <= 0
                                || r.state != (on ? 2 : 1)))) {
            throw new IllegalStateException("Actual canonical display proof required");
        }
    }

    static final class Context {
        final Object processor;
        final int display;
        final List<?> oldOn, oldOff;
        boolean target, failed;
        String boot;

        Context(Object p, int id) throws Exception {
            processor = p;
            display = id;
            target = (Boolean) get(p, "mScreenOnTarget");
            oldOn = new ArrayList<>((Stack<?>) get(p, "mOnStateCtrlList"));
            oldOff = new ArrayList<>((Stack<?>) get(p, "mOffStateCtrlList"));
        }
    }

    static final class Engine {
        final Platform platform;

        // Production uses the single ENGINE; host tests create isolated engines with their own
        // state.
        @SuppressWarnings("ThreadLocalUsage")
        final ThreadLocal<Context> current = new ThreadLocal<>();

        @SuppressWarnings("ThreadLocalUsage")
        final ThreadLocal<Object> rejected = new ThreadLocal<>();

        Engine(Platform p) {
            platform = p;
        }

        void sync(Object p, Reply r) throws Exception {
            field(p, "mScreenOnTarget").setBoolean(p, r.state == 2);
            call(p, "syncScreenState", new Class<?>[] {boolean.class}, r.state == 2);
        }

        void bootstrap(Object p) {
            try {
                int id = canonical(p);

                if (id < 0) {
                    return;
                }

                String boot = platform.boot();
                Reply r = platform.query(id);
                validate(r, boot, id, false, false);
                sync(p, r);
            } catch (Exception unavailable) {
                // checkstyle-allow IllegalCatch: OEM command boundary must reject failed
                // observation and restore original screen state.
                /* No reverse event or invented state on failed observation. */
            }
        }

        void before(Object p) {
            current.remove();
            rejected.remove();

            try {
                int id = canonical(p);

                if (id < 0) {
                    return;
                }

                Context c = new Context(p, id);
                current.set(c);

                try {
                    c.boot = platform.boot();
                    Reply r = platform.query(id);
                    validate(r, c.boot, id, false, false);
                    sync(p, r);
                    c.target = r.state == 2;
                } catch (Exception unavailable) {
                    // checkstyle-allow IllegalCatch: OEM command boundary must reject failed
                    // observation and restore original screen state.
                    c.failed = true;
                }
            } catch (Exception unsupported) {
                // checkstyle-allow IllegalCatch: OEM command boundary must reject failed
                // observation and restore original screen state.
                rejected.set(p);
            }
        }

        // Compares identity on purpose: the same Binder, token or instance, never an equal one.
        @SuppressWarnings("ReferenceEquality")
        boolean send(Object p, boolean on) {
            try {
                int id = canonical(p);

                if (id < 0) {
                    return false;
                }

                Context c = current.get();

                if (c == null || c.processor != p) {
                    before(p);
                    c = current.get();
                }

                if (c == null) {
                    return true;
                }

                if (c.failed) {
                    return true;
                }

                try {
                    Reply r = platform.set(id, on);
                    validate(r, c.boot, id, true, on);

                    try {
                        sync(p, r);
                    } catch (Exception reverseUnavailable) {
                        // checkstyle-allow IllegalCatch: OEM command boundary must reject failed
                        // observation and restore original screen state.
                        /* Hardware proof remains valid; do not invent a rollback of committed power. */
                    }
                } catch (Exception unavailable) {
                    // checkstyle-allow IllegalCatch: OEM command boundary must reject failed
                    // observation and restore original screen state.
                    c.failed = true;
                    restore(c);
                }

                return true;
            } catch (Exception unavailable) {
                // checkstyle-allow IllegalCatch: OEM command boundary must reject failed
                // observation and restore original screen state.
                Context c = current.get();

                if (c != null && c.processor == p) {
                    c.failed = true;
                }

                return true;
            }
        }

        @SuppressWarnings({"rawtypes", "unchecked"})
        void restore(Context c) {
            try {
                Stack on = (Stack) get(c.processor, "mOnStateCtrlList"),
                        off = (Stack) get(c.processor, "mOffStateCtrlList");
                on.clear();
                on.addAll(c.oldOn);
                off.clear();
                off.addAll(c.oldOff);
                field(c.processor, "mScreenOnTarget").setBoolean(c.processor, c.target);
            } catch (Exception broken) {
                // checkstyle-allow IllegalCatch: OEM command boundary must reject failed
                // observation and restore original screen state.
                throw new IllegalStateException("Pinned OEM policy restore failed", broken);
            }
        }

        // Compares identity on purpose: the same Binder, token or instance, never an equal one.
        @SuppressWarnings("ReferenceEquality")
        void result(Object p, Object listener, int value) throws Exception {
            Context c = current.get();

            try {
                if (rejected.get() == p) {
                    value = 2;
                }

                if (c != null && c.processor == p && c.failed) {
                    restore(c);
                    value = 2;
                }

                if (listener != null) {
                    call(listener, "onResult", new Class<?>[] {int.class}, value);
                }
            } finally {
                if (c != null && c.processor == p) {
                    current.remove();
                }

                if (rejected.get() == p) {
                    rejected.remove();
                }
            }
        }

        // Compares identity on purpose: the same Binder, token or instance, never an equal one.
        @SuppressWarnings("ReferenceEquality")
        void finish(Object p) {
            if (rejected.get() == p) {
                rejected.remove();
            }

            Context c = current.get();

            if (c != null && c.processor == p) {
                try {
                    if (c.failed) {
                        restore(c);
                    }
                } finally {
                    current.remove();
                }
            }
        }
    }

    static byte[] read(InputStream in, int limit) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        byte[] block = new byte[256];

        while (bytes.size() <= limit) {
            int count = in.read(block, 0, Math.min(block.length, limit + 1 - bytes.size()));

            if (count < 0) {
                break;
            }

            bytes.write(block, 0, count);
        }

        return bytes.toByteArray();
    }

    static final class Client implements Platform {
        @Override
        public String boot() throws Exception {
            try (InputStream in = new FileInputStream("/proc/sys/kernel/random/boot_id")) {
                String b = new String(read(in, 128), StandardCharsets.US_ASCII).trim();

                if (!b.matches("[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}")) {
                    throw new IOException("Boot fence unavailable");
                }

                return b;
            }
        }

        @Override
        public Reply query(int display) throws Exception {
            return command("query", display, null);
        }

        @Override
        public Reply set(int display, boolean on) throws Exception {
            return command("set", display, on ? "ON" : "OFF");
        }

        Reply command(String operation, int display, String state) throws Exception {
            if (display < 0 || display > 1) {
                throw new IllegalArgumentException("Canonical display required");
            }

            List<String> args =
                    new ArrayList<>(
                            Arrays.asList(
                                    "/system/bin/cmd",
                                    "display",
                                    "voyemu-passion-screen",
                                    operation,
                                    Integer.toString(display)));

            if (state != null) {
                args.add(state);
            }

            Process child = new ProcessBuilder(args).redirectErrorStream(true).start();

            try {
                if (!child.waitFor(2500, TimeUnit.MILLISECONDS)) {
                    child.destroyForcibly();
                    child.waitFor(500, TimeUnit.MILLISECONDS);
                    throw new IOException("Owned display command timeout; no ACK");
                }

                byte[] bytes;

                try (InputStream in = child.getInputStream()) {
                    bytes = read(in, 4096);
                }

                if (child.exitValue() != 0 || bytes.length > 4096) {
                    throw new IOException("Display command failed");
                }

                JSONObject j = new JSONObject(new String(bytes, StandardCharsets.UTF_8));

                if (!j.getBoolean("ok")) {
                    throw new IOException("No actual display response");
                }

                return new Reply(
                        j.getString("boot_id"),
                        j.getInt("display"),
                        j.getString("unique_id"),
                        j.getInt("state"),
                        j.getString("kind"),
                        new Observation(
                                j.optLong("request", 0),
                                j.optLong("transaction", 0),
                                j.getLong("observed_ms"),
                                j.getLong("order")));
            } finally {
                if (child.isAlive()) {
                    child.destroyForcibly();
                }

                try {
                    child.getOutputStream().close();
                } finally {
                    child.getInputStream().close();
                    child.getErrorStream().close();
                }
            }
        }
    }
}
