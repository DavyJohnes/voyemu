package com.qinggan.canbus.service;

public final class HfpPhoneInfoValuesTest {
    private static final String EPOCH = "12345678-1234-1234-1234-123456789abc";

    private static HfpPhoneInfoValues value(int state, long start, long now) {
        return HfpPhoneInfoValues.from(
                state,
                "+70000000001",
                new HfpPhoneInfoValues.Snapshot(
                        state, "+70000000001", "Иван Иванов", start, true, EPOCH),
                now);
    }

    private static void check(boolean condition) {
        if (!condition) {
            throw new AssertionError();
        }
    }

    private static void rejects(Runnable action) {
        try {
            action.run();
            throw new AssertionError("accepted malformed authority data");
        } catch (IllegalArgumentException expected) {
        }
    }

    public static void main(String[] args) {
        check("COMING_CALL".equals(value(130, 0, 9000).state));
        check("GOING_CALL".equals(value(129, 0, 9000).state));
        check("HANG_UP".equals(value(131, 0, 9000).state));
        HfpPhoneInfoValues late = value(128, 1000, 9800);
        check("CONNECTED".equals(late.state) && late.duration == 8);
        check("Иван Иванов".equals(late.name) && "+70000000001".equals(late.number));
        check(value(128, 1000, 15800).duration == 14);
        rejects(() -> value(127, 0, 9000));
        rejects(() -> value(128, 0, 9000));
        rejects(() -> value(130, 1000, 9000));
        rejects(() -> value(128, 10000, 9000));
        rejects(() -> value(128, 1, 0xffffffL * 1000 + 1001));
        rejects(() ->
                HfpPhoneInfoValues.from(
                        130,
                        "+70000000001",
                        new HfpPhoneInfoValues.Snapshot(129, "+70000000001", "", 0, true, EPOCH),
                        9000));
        rejects(() ->
                HfpPhoneInfoValues.from(
                        130,
                        "+70000000001",
                        new HfpPhoneInfoValues.Snapshot(130, "+70000000002", "", 0, true, EPOCH),
                        9000));
        rejects(() ->
                HfpPhoneInfoValues.from(
                        130,
                        "+70000000001",
                        new HfpPhoneInfoValues.Snapshot(130, "+70000000001", "", 0, false, EPOCH),
                        9000));
        rejects(() ->
                HfpPhoneInfoValues.from(
                        130,
                        "+70000000001",
                        new HfpPhoneInfoValues.Snapshot(130, "+70000000001", "", 0, true, "bad"),
                        9000));
        rejects(() ->
                HfpPhoneInfoValues.from(
                        130,
                        "70000000001",
                        new HfpPhoneInfoValues.Snapshot(130, "70000000001", "", 0, true, EPOCH),
                        9000));
        System.out.println("HfpPhoneInfoValuesTest PASS");
    }
}
