package com.voyemu.input;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.LinkedHashMap;
import java.util.Map;

/** Host phases are explicit; long/hold thresholds are adapter policy, not an OEM timing claim. */
public final class FreeWheelPhases {
    public static final long LONG_MS = 1000;

    public interface Watermark {
        void persist(long sequence) throws Exception;
    }

    private static final class Held {
        final String owner;
        final long time;

        Held(String owner, long time) {
            this.owner = owner;
            this.time = time;
        }
    }

    private final Map<String, Held> held = new LinkedHashMap<>();
    private long sequence;
    public final String session;
    private final String token;

    public FreeWheelPhases(String session, String token, long sequence) {
        if (session == null
                || !session.matches("[A-Za-z0-9_-]{8,64}")
                || token == null
                || !token.matches("[0-9a-f]{32}")
                || sequence < 0) {
            throw new IllegalArgumentException("invalid-session");
        }

        this.session = session;
        this.token = token;
        this.sequence = sequence;
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

    public synchronized int[] accept(
            long next, String button, String phase, String owner, long now, Watermark watermark)
            throws Exception {
        if (next <= sequence
                || next - sequence > 1000000
                || next == Long.MAX_VALUE
                || owner == null
                || !owner.matches("[A-Za-z0-9_.:-]{1,128}")
                || now < 0
                || watermark == null
                || key(button) == 0) {
            throw new IllegalArgumentException("invalid-input");
        }

        Held previous = held.get(button);
        int[] actions;

        if ("down".equals(phase)) {
            if (previous != null) {
                throw new IllegalArgumentException("already-held");
            }

            actions = new int[] {FreeWheelBroker.DOWN};
        } else if ("up".equals(phase) || "cancel".equals(phase)) {
            actions = releaseActions(previous, button, phase, owner, now);
        } else {
            throw new IllegalArgumentException("invalid-phase");
        }

        // Durable consumption precedes side effects; failed persistence cannot create a phantom
        // hold.
        watermark.persist(next);
        sequence = next;

        if ("down".equals(phase)) {
            held.put(button, new Held(owner, now));
        } else {
            held.remove(button);
        }

        return actions;
    }

    private static int[] releaseActions(
            Held previous, String button, String phase, String owner, long now) {
        if (previous == null
                || !previous.owner.equals(owner)
                || (previous.time >= 0 && now < previous.time)) {
            throw new IllegalArgumentException("not-owner");
        }

        if ("cancel".equals(phase) || previous.time < 0) {
            return new int[] {FreeWheelBroker.UP};
        }
        // Only recovered photo has a proven long-start consumer. No invented mute/voice long
        // action.
        else if ("photo".equals(button) && now - previous.time >= LONG_MS) {
            return new int[] {FreeWheelBroker.LONG, FreeWheelBroker.LONG_STOP, FreeWheelBroker.UP};
        } else {
            return new int[] {FreeWheelBroker.SHORT, FreeWheelBroker.UP};
        }
    }

    public synchronized long sequence() {
        return sequence;
    }

    public synchronized int heldCount() {
        return held.size();
    }

    public synchronized Map<String, String> holds() {
        Map<String, String> result = new LinkedHashMap<>();

        for (Map.Entry<String, Held> entry : held.entrySet()) {
            result.put(entry.getKey(), entry.getValue().owner);
        }

        return result;
    }

    public synchronized void restore(Map<String, String> values) {
        if (!held.isEmpty() || values.size() > 10) {
            throw new IllegalArgumentException("invalid-restored-holds");
        }

        for (Map.Entry<String, String> entry : values.entrySet()) {
            if (key(entry.getKey()) == 0
                    || entry.getValue() == null
                    || !entry.getValue().matches("[A-Za-z0-9_.:-]{1,128}")) {
                throw new IllegalArgumentException("invalid-restored-holds");
            }
        }

        // A process restart has no trustworthy down timestamp: release only, never synthesize a
        // click.
        for (Map.Entry<String, String> entry : values.entrySet()) {
            held.put(entry.getKey(), new Held(entry.getValue(), -1));
        }
    }
}
