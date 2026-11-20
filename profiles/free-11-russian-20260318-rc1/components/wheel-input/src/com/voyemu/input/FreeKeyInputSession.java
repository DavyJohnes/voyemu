package com.voyemu.input;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.LinkedHashMap;
import java.util.Map;

/** Explicit host phases only. The genuine OEM service owns short/hold/long processing. */
public final class FreeKeyInputSession {
    public interface Watermark {
        void persist(long sequence, Map<String, String> holds) throws Exception;
    }

    public interface Wire {
        void token(String value);

        void number(int value);

        void nullBundle();

        void text(String value);
    }

    public static final class Event {
        public final int key, action, flags;
        private String button;

        Event(int key, int action, int flags) {
            this.key = key;
            this.action = action;
            this.flags = flags;
        }
    }

    public final String session;
    private final String token;
    private long sequence;
    private Event pending;
    private boolean uncertain;
    private final Map<String, String> holds = new LinkedHashMap<>();

    public FreeKeyInputSession(
            String session, String token, long sequence, Map<String, String> holds) {
        this(session, token, sequence, holds, false);
    }

    public FreeKeyInputSession(
            String session,
            String token,
            long sequence,
            Map<String, String> holds,
            boolean uncertain) {
        if (session == null
                || !session.matches("[A-Za-z0-9_-]{8,64}")
                || token == null
                || !token.matches("[0-9a-f]{32}")
                || sequence < 0
                || holds == null
                || holds.size() > 10) {
            throw new IllegalArgumentException("free-wheel-session");
        }

        for (Map.Entry<String, String> entry : holds.entrySet()) {
            if (key(entry.getKey()) == 0 || !owner(entry.getValue())) {
                throw new IllegalArgumentException("free-wheel-holds");
            }
        }

        this.session = session;
        this.token = token;
        this.sequence = sequence;
        this.holds.putAll(holds);
        this.uncertain = uncertain;
    }

    public static boolean callerAllowed(int uid, boolean permission) {
        return permission && (uid == 0 || uid == 2000);
    }

    private static boolean owner(String value) {
        return value != null && value.matches("[A-Za-z0-9_.:-]{1,128}");
    }

    public boolean authenticates(int schema, String session, String token) {
        return schema == 1
                && this.session.equals(session)
                && token != null
                && MessageDigest.isEqual(
                        this.token.getBytes(StandardCharsets.US_ASCII),
                        token.getBytes(StandardCharsets.US_ASCII));
    }

    public static int key(String button) {
        if (button == null) {
            return 0;
        }

        switch (button) {
            case "volume_up":
                return 2;
            case "volume_down":
                return 1;
            case "previous":
                return 4;
            case "next":
                return 3;
            case "ok":
                return 6;
            case "phone":
                return 128;
            case "voice":
                return 130;
            case "photo":
                return 173;
            case "custom":
                return 3090;
            case "menu":
                return 3074;
            default:
                return 0;
        }
    }

    public synchronized Event accept(
            long next, String button, String phase, String owner, Watermark storage)
            throws Exception {
        if (uncertain) {
            throw new IllegalArgumentException("free-wheel-pending-submission");
        }

        if (next <= sequence
                || next - sequence > 1000000
                || next == Long.MAX_VALUE
                || key(button) == 0
                || !owner(owner)
                || storage == null) {
            throw new IllegalArgumentException("free-wheel-input");
        }

        Map<String, String> pending = new LinkedHashMap<>(holds);
        int action, flags = 0;

        if ("down".equals(phase)) {
            if (pending.containsKey(button)) {
                throw new IllegalArgumentException("free-wheel-already-held");
            }

            pending.put(button, owner);
            action = 4096;
        } else if ("up".equals(phase) || "cancel".equals(phase)) {
            if (!owner.equals(pending.get(button))) {
                throw new IllegalArgumentException("free-wheel-not-owner");
            }

            // Retain the durable hold until Binder completion is confirmed.
            action = 65536;

            if ("cancel".equals(phase)) {
                flags = 32;
            }
        } else {
            throw new IllegalArgumentException("free-wheel-phase");
        }

        storage.persist(next, new LinkedHashMap<>(pending));
        sequence = next;
        holds.clear();
        holds.putAll(pending);
        Event event = new Event(key(button), action, flags);
        event.button = button;
        this.pending = event;
        uncertain = true;

        return event;
    }

    public synchronized void confirmed(Event event, Watermark storage) throws Exception {
        if (!uncertain || pending != event || storage == null) {
            throw new IllegalArgumentException("free-wheel-confirmation");
        }

        Map<String, String> released = new LinkedHashMap<>(holds);

        if (event.action == 65536) {
            released.remove(event.button);
        }

        storage.persist(sequence, new LinkedHashMap<>(released));
        holds.clear();
        holds.putAll(released);
        pending = null;
        uncertain = false;
    }

    public synchronized boolean pendingSubmission() {
        return uncertain;
    }

    public synchronized long sequence() {
        return sequence;
    }

    public synchronized boolean hasHolds() {
        return uncertain || !holds.isEmpty();
    }

    public static void writeEvent(Wire wire, Event event) {
        wire.token("com.qinggan.system.IKeyManagerService");
        wire.number(1);
        wire.number(event.key);
        wire.number(event.action);
        wire.number(event.flags);
        wire.nullBundle();
        wire.number(-1);
        wire.text("com.voyemu.input.wheel");
    }
}
