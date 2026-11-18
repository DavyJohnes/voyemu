package com.voyemu.input;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.LinkedHashMap;
import java.util.Map;

/** Session and ownership boundary for Android media keys; no OEM effects are inferred. */
public final class PassionWheelSession {
    public interface Watermark {
        void persist(long sequence, Map<String, String> holds) throws Exception;
    }

    public static final class Event {
        public final int key, action, flags;
        private final String button;

        Event(int key, int action, int flags, String button) {
            this.key = key;
            this.action = action;
            this.flags = flags;
            this.button = button;
        }
    }

    public final String session;
    private final String token;
    private long sequence;
    private boolean uncertain;
    private Event pending;
    private final Map<String, String> holds = new LinkedHashMap<>();

    public PassionWheelSession(
            String session, String token, long sequence, Map<String, String> holds) {
        this(session, token, sequence, holds, false);
    }

    public PassionWheelSession(
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
                || holds.size() > 5) {
            throw new IllegalArgumentException("passion-wheel-session");
        }

        for (Map.Entry<String, String> e : holds.entrySet()) {
            if (key(e.getKey()) == 0 || !owner(e.getValue())) {
                throw new IllegalArgumentException("passion-wheel-holds");
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
            case "ok":
                return 85;
            case "next":
                return 87;
            case "previous":
                return 88;
            case "volume_up":
                return 24;
            case "volume_down":
                return 25;
            default:
                return 0;
        }
    }

    public synchronized Event accept(
            long next, String button, String phase, String owner, Watermark storage)
            throws Exception {
        if (uncertain
                || next <= sequence
                || next - sequence > 1000000
                || next == Long.MAX_VALUE
                || key(button) == 0
                || !owner(owner)
                || storage == null) {
            throw new IllegalArgumentException("passion-wheel-input");
        }

        Map<String, String> updated = new LinkedHashMap<>(holds);
        int action;

        if ("down".equals(phase)) {
            if (updated.containsKey(button)) {
                throw new IllegalArgumentException("passion-wheel-held");
            }

            updated.put(button, owner);
            action = 0;
        } else if ("up".equals(phase) || "cancel".equals(phase)) {
            if (!owner.equals(updated.get(button))) {
                throw new IllegalArgumentException("passion-wheel-owner");
            }

            action = 1;
        } else {
            throw new IllegalArgumentException("passion-wheel-phase");
        }

        storage.persist(next, new LinkedHashMap<>(updated));
        sequence = next;
        holds.clear();
        holds.putAll(updated);
        pending = new Event(key(button), action, "cancel".equals(phase) ? 32 : 0, button);
        uncertain = true;

        return pending;
    }

    public synchronized void confirmed(Event event, Watermark storage) throws Exception {
        if (!uncertain || pending != event || storage == null) {
            throw new IllegalArgumentException("passion-wheel-confirmation");
        }

        Map<String, String> updated = new LinkedHashMap<>(holds);

        if (event.action == 1) {
            updated.remove(event.button);
        }

        storage.persist(sequence, new LinkedHashMap<>(updated));
        holds.clear();
        holds.putAll(updated);
        pending = null;
        uncertain = false;
    }

    public synchronized long sequence() {
        return sequence;
    }

    public synchronized boolean hasHolds() {
        return uncertain || !holds.isEmpty();
    }

    public synchronized boolean pendingSubmission() {
        return uncertain;
    }
}
