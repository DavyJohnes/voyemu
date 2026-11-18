package com.voyemu.input;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** OEM callback routing policy; Android-free so the real policy is host-testable. */
public final class KeyEventRouter {
    public interface Callback {
        boolean onEvent(int code, int action, String registration, int type) throws Exception;
    }

    public static final class Delivery {
        public int callbacks;
        public int terminals;
        public boolean consumed;
        public boolean restricted;
        public boolean srcNotified;
        public AudioControl.VolumeChange volume;
        public String terminalFailure;
        public Map<String, Object> terminalDiagnostics;
        public final List<Map<String, Object>> callbackReceipts = new ArrayList<>();

        void add(Delivery other) {
            callbacks += other.callbacks;
            terminals += other.terminals;
            consumed |= other.consumed;
            restricted |= other.restricted;
            srcNotified |= other.srcNotified;

            if (other.volume != null) {
                volume = other.volume;
            }

            if (other.terminalFailure != null) {
                terminalFailure = other.terminalFailure;
            }

            if (other.terminalDiagnostics != null) {
                terminalDiagnostics = other.terminalDiagnostics;
            }

            for (Map<String, Object> receipt : other.callbackReceipts) {
                if (callbackReceipts.size() == 8) {
                    break;
                }

                callbackReceipts.add(receipt);
            }
        }
    }

    private static final class Listener {
        final String pkg;
        final int type;
        final int priority;
        final int[] keys;
        final Callback callback;

        Listener(String pkg, int type, int priority, int[] keys, Callback callback) {
            this.pkg = pkg;
            this.type = type;
            this.priority = priority;
            this.keys = keys == null ? new int[0] : keys.clone();
            this.callback = callback;
        }

        boolean watches(int code) {
            if (keys.length == 0) {
                return true;
            }

            for (int key : keys) {
                if (key == code) {
                    return true;
                }
            }

            return false;
        }
    }

    private final Map<String, Listener> listeners = new LinkedHashMap<>();
    private final Map<Integer, Integer> mappings = new LinkedHashMap<>();
    private boolean restricted;
    private int[] unrestricted = new int[0];
    private String mediaOwner = "com.qinggan.media";

    public KeyEventRouter() {
        // Matching TranslatorSink.loadMappingCfg(): physical star defaults to SRC.
        mappings.put(3090, 65);
    }

    private static String id(String pkg, int type) {
        return pkg + "#" + type;
    }

    public synchronized boolean register(
            String pkg, int type, int priority, int[] keys, Callback callback) {
        if (pkg == null
                || pkg.length() == 0
                || pkg.length() > 256
                || callback == null
                || (type != 0 && type != 1)
                || (keys != null && keys.length > 256)) {
            return false;
        }

        String key = id(pkg, type);

        if (!listeners.containsKey(key) && listeners.size() >= 128) {
            return false;
        }

        listeners.put(key, new Listener(pkg, type, priority, keys, callback));

        return true;
    }

    public synchronized void unregister(String pkg, int type, Callback callback) {
        Listener listener = listeners.get(id(pkg, type));

        if (listener != null && listener.callback == callback) {
            listeners.remove(id(pkg, type));
        }
    }

    public synchronized void restriction(boolean enabled, int[] allowed) {
        restricted = enabled;
        unrestricted = allowed == null ? new int[0] : allowed.clone();
    }

    public synchronized void mapping(int from, int to) {
        if (from > 0 && to > 0) {
            mappings.put(from, to);
        }
    }

    public synchronized int mapping(int code) {
        Integer mapped = mappings.get(code);

        return mapped == null ? code : mapped;
    }

    public synchronized void mediaOwner(String pkg) {
        if (pkg != null && pkg.length() > 0) {
            mediaOwner = pkg;
        }
    }

    private static boolean mediaKey(int code) {
        return code == 3
                || code == 4
                || code == 6
                || code == 7
                || code == 8
                || code == 9
                || code == 13
                || code == 14
                || (code >= 201 && code <= 204);
    }

    public Delivery dispatch(int inputCode, int action) {
        Delivery result = new Delivery();
        final int code;
        final List<Listener> intercepts = new ArrayList<>();
        final List<Listener> regular = new ArrayList<>();
        final String target;

        synchronized (this) {
            code = mapping(inputCode);
            boolean allowed = false;

            for (int key : unrestricted) {
                if (key == code) {
                    allowed = true;
                }
            }

            // Never leave a consumer pressed merely because restriction changed mid-hold.
            if (restricted && !allowed && action != 65536 && action != 262144 && action != 524288) {
                result.restricted = true;

                return result;
            }

            target = mediaOwner;
            partition(code, target, intercepts, regular);
        }

        intercepts.sort(
                Comparator.comparingInt((Listener listener) -> listener.priority).reversed());

        for (Listener listener : intercepts) {
            if (invoke(listener, code, action, result)) {
                return result;
            }
        }

        // Normal OEM notifications intentionally visit each filtered registration once;
        // onEvent selects that client's NORMAL listener, whereas onKeyEvent fans out again.
        for (Listener listener : regular) {
            invoke(listener, code, action, result);
        }

        return result;
    }

    /** Splits the listeners watching code; the caller holds this lock. */
    private void partition(
            int code, String target, List<Listener> intercepts, List<Listener> regular) {
        for (Listener listener : listeners.values()) {
            if (!listener.watches(code)) {
                continue;
            }

            if (listener.type == 1) {
                // Matching EventListenerSink.UNINTERCEPT_KEYS excludes volume, except factory.
                if ((code != 1 && code != 2)
                        || "com.qinggan.app.factorytest".equals(listener.pkg)) {
                    intercepts.add(listener);
                }
            } else if (code != 1 && code != 2) {
                if (!mediaKey(code) || listener.pkg.equals(target)) {
                    regular.add(listener);
                }
            }
        }
    }

    private boolean invoke(Listener listener, int code, int action, Delivery result) {
        synchronized (this) {
            if (listeners.get(id(listener.pkg, listener.type)) != listener) {
                return false;
            }
        }

        try {
            boolean consumed = listener.callback.onEvent(code, action, listener.pkg, listener.type);
            result.callbacks++;

            if (code == 65
                    && action == WheelInputSession.SHORT
                    && "com.qinggan.media".equals(listener.pkg)) {
                result.srcNotified = true;
            }

            if (result.callbackReceipts.size() < 8) {
                Map<String, Object> receipt = new LinkedHashMap<>();
                receipt.put("registration", listener.pkg);
                receipt.put("type", listener.type);
                receipt.put("key_code", code);
                receipt.put("action", action);
                receipt.put("consumed", consumed);
                result.callbackReceipts.add(receipt);
            }

            result.consumed |= consumed;

            return consumed;
        } catch (Exception error) {
            // checkstyle-allow IllegalCatch: a failing OEM key listener is unregistered
            unregister(listener.pkg, listener.type, listener.callback);

            return false;
        }
    }
}
