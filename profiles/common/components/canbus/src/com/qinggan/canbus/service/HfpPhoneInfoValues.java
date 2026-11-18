package com.qinggan.canbus.service;

/** Validated primitive data from the authenticated Phone authority; no local call clock. */
final class HfpPhoneInfoValues {
    final String state, number, name;
    final int duration;

    private HfpPhoneInfoValues(String state, String number, String name, int duration) {
        this.state = state;
        this.number = number;
        this.name = name;
        this.duration = duration;
    }

    /** One authority snapshot as read from the Phone PhoneInfo Bundle. */
    static final class Snapshot {
        final int state;
        final String number, name, incarnation;
        final long activeStarted;
        final boolean connected;

        Snapshot(
                int state,
                String number,
                String name,
                long activeStarted,
                boolean connected,
                String incarnation) {
            this.state = state;
            this.number = number;
            this.name = name;
            this.activeStarted = activeStarted;
            this.connected = connected;
            this.incarnation = incarnation;
        }
    }

    static HfpPhoneInfoValues from(
            int callbackState, String callbackNumber, Snapshot snapshot, long now) {
        int state = snapshot.state;
        String number = snapshot.number, name = snapshot.name;
        long activeStarted = snapshot.activeStarted;

        if (!consistent(callbackState, callbackNumber, snapshot, now)) {
            throw new IllegalArgumentException("PHONE_INFO_SNAPSHOT");
        }

        requireName(name);
        String mapped = mappedState(state);

        if ((state == 128) != (activeStarted > 0)) {
            throw new IllegalArgumentException("PHONE_INFO_CLOCK");
        }

        long seconds = activeStarted == 0 ? 0 : (now - activeStarted) / 1000;

        if (seconds > 0xffffff) {
            throw new IllegalArgumentException("PHONE_INFO_DURATION");
        }

        return new HfpPhoneInfoValues(mapped, number, name, (int) seconds);
    }

    private static boolean consistent(
            int callbackState, String callbackNumber, Snapshot s, long now) {
        boolean unavailable =
                s.state == 131
                        && !s.connected
                        && "".equals(s.number)
                        && "".equals(s.name)
                        && s.activeStarted == 0;

        return callbackState == s.state
                && s.number != null
                && s.number.equals(callbackNumber)
                && (unavailable || s.number.matches("\\+[0-9]{7,15}"))
                && s.name != null
                && s.name.length() <= 128
                && s.name.indexOf('\0') < 0
                && (s.incarnation == null
                        ? unavailable
                        : s.incarnation.matches(
                                "[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"))
                && now >= 0
                && s.activeStarted >= 0
                && s.activeStarted <= now
                && (s.connected || s.state == 131);
    }

    private static void requireName(String name) {
        for (int i = 0; i < name.length(); i++) {
            char c = name.charAt(i);

            if (Character.isHighSurrogate(c)) {
                if (++i >= name.length() || !Character.isLowSurrogate(name.charAt(i))) {
                    throw new IllegalArgumentException("PHONE_INFO_NAME");
                }
            } else if (Character.isLowSurrogate(c)) {
                throw new IllegalArgumentException("PHONE_INFO_NAME");
            }
        }
    }

    private static String mappedState(int state) {
        if (state == 128) {
            return "CONNECTED";
        } else if (state == 129) {
            return "GOING_CALL";
        } else if (state == 130) {
            return "COMING_CALL";
        } else if (state == 131) {
            return "HANG_UP";
        }

        throw new IllegalArgumentException("PHONE_INFO_STATE");
    }
}
