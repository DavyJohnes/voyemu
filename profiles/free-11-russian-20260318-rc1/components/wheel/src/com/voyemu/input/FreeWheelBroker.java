package com.voyemu.input;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Own bounded listener broker; callback completion never proves an OEM visible effect. */
public final class FreeWheelBroker {
    public static final String OWNER_SHA256 =
            "c178682be164be4872697dbb5accdc6ea28446941b6d681ddb736f2fdb627c07";
    public static final String SERVICE = "com.qinggan.system.IKeyManagerService";
    public static final String CALLBACK = "com.qinggan.system.IKeyManagerCallback";
    public static final int DOWN = 4096, SHORT = 8192, HOLD = 16384, LONG = 32768;
    public static final int UP = 65536, HOLD_STOP = 262144, LONG_STOP = 524288;

    public interface Callback {
        boolean event(int key, int action, String registration, int type) throws Exception;
    }

    public interface Wire {
        void token(String value);

        void number(int value);

        void text(String value);

        void nullBundle();
    }

    public static void writeCallback(
            Wire wire, int key, int action, String registration, int type) {
        wire.token(CALLBACK);
        wire.number(1);
        wire.number(key);
        wire.number(action);
        wire.number(0);
        wire.nullBundle();
        wire.number(-1);
        wire.text(registration);
        wire.number(type);
    }

    public static final class Result {
        public int attempted, delivered, handled, failed;
        public final List<String> registrations = new ArrayList<>();

        public void add(Result other) {
            attempted += other.attempted;
            delivered += other.delivered;
            handled += other.handled;
            failed += other.failed;

            for (String name : other.registrations) {
                if (registrations.size() < 32) {
                    registrations.add(name);
                }
            }
        }

        public String status() {
            return failed > 0
                    ? "terminal-unavailable"
                    : delivered > 0 ? "callbacks" : "no-consumer";
        }
    }

    private static final class Listener {
        final int uid, type, priority;
        final String name;
        final int[] keys;
        final Object identity;
        final Callback callback;

        Listener(
                int uid,
                int type,
                int priority,
                String name,
                int[] keys,
                Object identity,
                Callback callback) {
            this.uid = uid;
            this.type = type;
            this.priority = priority;
            this.name = name;
            this.keys = keys.clone();
            this.identity = identity;
            this.callback = callback;
        }

        boolean accepts(int key) {
            return keys.length == 0 || Arrays.stream(keys).anyMatch(value -> value == key);
        }
    }

    private final Map<String, Listener> listeners = new LinkedHashMap<>();

    public static boolean controlCallerAllowed(int uid, boolean dumpPermission) {
        return dumpPermission && (uid == 0 || uid == 2000);
    }

    private static String id(String name, int type) {
        return type + ":" + name;
    }

    public static boolean registrationAllowed(String name, String[] packages) {
        if (name == null || name.length() > 200 || packages == null) {
            return false;
        }

        for (String pkg : packages) {
            if (pkg != null && (name.equals(pkg) || name.equals(pkg + "_keyeventtool"))) {
                return true;
            }
        }

        return false;
    }

    public synchronized int register(
            // checkstyle-allow ParameterNumber: OEM registration keeps owner identity and callback
            // together
            int uid,
            String name,
            int type,
            int priority,
            int flags,
            int[] keys,
            Object identity,
            Callback callback) {
        if (uid < 0
                || name == null
                || !name.matches("[A-Za-z0-9_.]{1,200}")
                || type < 0
                || type > 1
                || flags != 1
                || keys == null
                || keys.length > 64
                || identity == null
                || callback == null) {
            return -1;
        }

        for (int key : keys) {
            if (key <= 0 || key > 65535) {
                return -1;
            }
        }

        String id = id(name, type);
        Listener previous = listeners.get(id);

        if ((previous != null && previous.uid != uid)
                || (previous == null && listeners.size() >= 32)) {
            return -1;
        }

        listeners.put(id, new Listener(uid, type, priority, name, keys, identity, callback));

        return 0;
    }

    public synchronized boolean unregister(int uid, String name, int type, Object identity) {
        Listener value = listeners.get(id(name, type));

        if (value == null || value.uid != uid || !value.identity.equals(identity)) {
            return false;
        }

        listeners.remove(id(name, type));

        return true;
    }

    public synchronized int size() {
        return listeners.size();
    }

    public Result dispatch(int key, int action) {
        if (key <= 0
                || key > 65535
                || (action != DOWN
                        && action != SHORT
                        && action != UP
                        && action != HOLD
                        && action != LONG
                        && action != HOLD_STOP
                        && action != LONG_STOP)) {
            throw new IllegalArgumentException("invalid-key-event");
        }

        List<Listener> snapshot;

        synchronized (this) {
            snapshot = new ArrayList<>(listeners.values());
        }

        // Own policy: intercept priority first, then normal listeners; only true intercept
        // consumes.
        snapshot.sort(
                Comparator.comparingInt((Listener value) -> -value.type)
                        .thenComparing(
                                Comparator.comparingInt((Listener value) -> value.priority)
                                        .reversed()));
        Result result = new Result();

        for (Listener value : snapshot) {
            if (!value.accepts(key)) {
                continue;
            }

            synchronized (this) {
                if (listeners.get(id(value.name, value.type)) != value) {
                    continue;
                }
            }

            result.attempted++;

            try {
                boolean handled = value.callback.event(key, action, value.name, value.type);
                result.delivered++;
                result.registrations.add(value.name);

                if (handled) {
                    result.handled++;
                }

                if (handled && value.type == 1) {
                    break;
                }
            } catch (Exception failure) {
                // checkstyle-allow IllegalCatch: one failed OEM callback is counted and removed
                // without stopping later listeners
                result.failed++;

                synchronized (this) {
                    if (listeners.get(id(value.name, value.type)) == value) {
                        listeners.remove(id(value.name, value.type));
                    }
                }
            }
        }

        return result;
    }
}
