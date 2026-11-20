package com.voyemu.input;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.function.LongSupplier;

/** Authenticated transient phases; sequence watermark is persisted before dispatch. */
public final class WheelInputSession {
    static final int SCHEMA = 1;
    public static final int DOWN = 4096;
    public static final int SHORT = 8192;
    public static final int LONG = 32768;
    public static final int LONG_STOP = 524288;
    public static final int UP = 65536;
    // Matching OEM KeyManagerReader.Processor: hold at 1000 ms, long immediately afterwards.
    static final long LONG_PRESS_MS = 1000;
    final String session;
    private final String token;
    private long sequence;
    private final Map<String, String> held = new LinkedHashMap<>();
    private final Map<String, Long> downTimes = new LinkedHashMap<>();
    private final Map<String, Boolean> longPressed = new LinkedHashMap<>();
    private final LongSupplier clock;

    static final class Command {
        final long sequence;
        final String button;
        final String phase;
        final String owner;
        final int key;
        final int[] actions;
        final long acceptedAt;

        Command(
                long sequence,
                String button,
                String phase,
                String owner,
                int key,
                int[] actions,
                long acceptedAt) {
            this.sequence = sequence;
            this.button = button;
            this.phase = phase;
            this.owner = owner;
            this.key = key;
            this.actions = actions;
            this.acceptedAt = acceptedAt;
        }
    }

    WheelInputSession(String session, String token, long sequence) {
        this(session, token, sequence, () -> System.nanoTime() / 1000000L);
    }

    WheelInputSession(String session, String token, long sequence, LongSupplier clock) {
        if (session == null
                || !session.matches("[A-Za-z0-9_-]{8,64}")
                || token == null
                || !token.matches("[0-9a-f]{32}")
                || sequence < 0
                || clock == null) {
            throw new IllegalArgumentException("invalid-session");
        }

        this.session = session;
        this.token = token;
        this.sequence = sequence;
        this.clock = clock;
    }

    boolean authenticates(int schema, String candidateSession, String candidateToken) {
        if (schema != SCHEMA || !session.equals(candidateSession) || candidateToken == null) {
            return false;
        }

        return MessageDigest.isEqual(
                token.getBytes(StandardCharsets.US_ASCII),
                candidateToken.getBytes(StandardCharsets.US_ASCII));
    }

    static int schemaValue(Object value) {
        if (!(value instanceof Integer) && !(value instanceof Long)) {
            return -1;
        }

        return ((Number) value).longValue() == SCHEMA ? SCHEMA : -1;
    }

    static boolean bootstrapMatches(
            int schema,
            String session,
            String token,
            String previousSession,
            String previousToken) {
        if (schema != SCHEMA) {
            throw new IllegalArgumentException("invalid-schema");
        }

        WheelInputSession candidate = new WheelInputSession(session, token, 0);

        if (previousSession == null || !session.equals(previousSession)) {
            return false;
        }

        if (!candidate.authenticates(schema, previousSession, previousToken)) {
            throw new IllegalArgumentException("session-token-change");
        }

        return true;
    }

    static boolean controlCallerAllowed(int uid, boolean dumpPermission) {
        return (uid == 0 || uid == 2000) && dumpPermission;
    }

    static int keyFor(String button) {
        if (button == null) {
            return 0;
        }

        switch (button) {
            case "custom":
                return 3090;
            case "menu":
                return 3074;
            case "voice":
                return 130;
            case "volume_up":
                return 2;
            case "ok":
                return 6;
            case "volume_down":
                return 1;
            case "previous":
                return 4;
            case "next":
                return 3;
            case "photo":
                return 173;
            case "phone":
                return 128;
            default:
                return 0;
        }
    }

    synchronized Command prepare(long next, String button, String phase, String owner) {
        if (next <= sequence || next - sequence > 1000000L || next >= Long.MAX_VALUE) {
            throw new IllegalArgumentException("replayed-sequence");
        }

        if (owner == null || !owner.matches("[A-Za-z0-9_.:-]{1,128}")) {
            throw new IllegalArgumentException("invalid-owner");
        }

        int key = keyFor(button);

        if (key == 0) {
            throw new IllegalArgumentException("unmapped-button");
        }

        String previous = held.get(button);
        long now = clock.getAsLong();
        int[] actions;

        if ("down".equals(phase)) {
            if (previous != null) {
                throw new IllegalArgumentException("already-held");
            }

            actions = new int[] {DOWN};
        } else if ("up".equals(phase) || "cancel".equals(phase)) {
            if (!owner.equals(previous)) {
                throw new IllegalArgumentException("not-owner");
            }

            if ("cancel".equals(phase)) {
                actions = new int[] {UP};
            } else if (supportsLong(button)
                    && downTimes.containsKey(button)
                    && now - downTimes.get(button) >= LONG_PRESS_MS) {
                actions =
                        longPressed.containsKey(button)
                                ? new int[] {LONG_STOP, UP}
                                : new int[] {LONG, LONG_STOP, UP};
            } else {
                actions = new int[] {SHORT, UP};
            }
        } else {
            throw new IllegalArgumentException("invalid-phase");
        }

        return new Command(next, button, phase, owner, key, actions, now);
    }

    static boolean supportsLong(String button) {
        // These recovered OEM consumers have long-start actions and no long-stop side effect.
        // Requested emulator policy: held volume-down toggles the recovered OEM mute terminal.
        // OEM physical QG1 LONG itself repeats volume-down; this is an explicit adapter policy.
        return "custom".equals(button)
                || "photo".equals(button)
                || "phone".equals(button)
                || "volume_down".equals(button);
    }

    synchronized boolean startLong(String button, String owner) {
        if (!supportsLong(button)
                || !owner.equals(held.get(button))
                || longPressed.containsKey(button)
                || !downTimes.containsKey(button)
                || clock.getAsLong() - downTimes.get(button) < LONG_PRESS_MS) {
            return false;
        }

        longPressed.put(button, true);

        return true;
    }

    synchronized void commit(Command command) {
        if (command.sequence <= sequence) {
            throw new IllegalArgumentException("replayed-sequence");
        }

        sequence = command.sequence;

        if ("down".equals(command.phase)) {
            held.put(command.button, command.owner);
            downTimes.put(command.button, command.acceptedAt);
        } else {
            held.remove(command.button);
            downTimes.remove(command.button);
            longPressed.remove(command.button);
        }
    }

    synchronized void restoreHeld(Map<String, String> values) {
        if (values.size() > 10) {
            throw new IllegalArgumentException("invalid-holds");
        }

        for (Map.Entry<String, String> entry : values.entrySet()) {
            if (keyFor(entry.getKey()) == 0
                    || entry.getValue() == null
                    || !entry.getValue().matches("[A-Za-z0-9_.:-]{1,128}")) {
                throw new IllegalArgumentException("invalid-holds");
            }
        }

        held.clear();
        downTimes.clear();
        longPressed.clear();
        held.putAll(values);
    }

    synchronized void clearHeld() {
        held.clear();
        downTimes.clear();
        longPressed.clear();
    }

    synchronized long sequence() {
        return sequence;
    }

    synchronized Map<String, String> held() {
        return new LinkedHashMap<>(held);
    }
}
