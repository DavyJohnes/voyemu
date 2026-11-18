package com.voyemu.vehicle;

import java.util.HashMap;
import java.util.Map;

public final class PassionMcuProtocolTest {
    static final String BOOT = "12345678-1234-1234-1234-123456789abc",
            SHA = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    static Map<String, Object> valid() {
        Map<String, Object> v = new HashMap<>();

        v.put("schema", 1);
        v.put("boot", BOOT);
        v.put("sequence", 1L);
        v.put("soc", 55);
        v.put("source_sha256", SHA);

        return v;
    }

    static void rejects(Runnable r) {
        try {
            r.run();
        } catch (IllegalArgumentException | SecurityException expected) {
            return;
        }

        throw new AssertionError("invalid protocol accepted");
    }

    public static void main(String[] args) {
        boolean ea = false;

        assert ea = true;

        if (!ea) {
            throw new AssertionError("-ea required");
        }

        PassionMcuProtocol.authorize(0, true, 1000, "com.mega.car");
        PassionMcuProtocol.authorize(2000, true, 1000, "com.mega.car");

        for (int caller : new int[] {1000, 1001, 10000, -1}) {
            rejects(() -> PassionMcuProtocol.authorize(caller, true, 1000, "com.mega.car"));
        }

        rejects(() -> PassionMcuProtocol.authorize(0, false, 1000, "com.mega.car"));
        rejects(() -> PassionMcuProtocol.authorize(0, true, 10000, "com.mega.car"));
        rejects(() -> PassionMcuProtocol.authorize(0, true, 1000, "foreign"));
        var v = valid();
        assert PassionMcuProtocol.decode(v, BOOT).soc == 55;

        for (var entry :
                Map.<String, Object>of(
                                "schema",
                                1L,
                                "boot",
                                "22345678-1234-1234-1234-123456789abc",
                                "sequence",
                                1,
                                "soc",
                                55.0,
                                "source_sha256",
                                SHA + "a")
                        .entrySet()) {
            var bad = valid();
            bad.put(entry.getKey(), entry.getValue());
            rejects(() -> PassionMcuProtocol.decode(bad, BOOT));
        }

        for (Object seq : new Object[] {0L, -1L, 0x100000000L, 1.0, null}) {
            var bad = valid();
            bad.put("sequence", seq);
            rejects(() -> PassionMcuProtocol.decode(bad, BOOT));
        }

        for (Object soc : new Object[] {-1, 101, 55L, "55", null}) {
            var bad = valid();
            bad.put("soc", soc);
            rejects(() -> PassionMcuProtocol.decode(bad, BOOT));
        }

        for (String key : valid().keySet()) {
            var bad = valid();
            bad.remove(key);
            rejects(() -> PassionMcuProtocol.decode(bad, BOOT));
        }

        var bad = valid();
        bad.put("target", 80);
        rejects(() -> PassionMcuProtocol.decode(bad, BOOT));
        var max = valid();
        max.put("sequence", 0xffffffffL);
        max.put("soc", 100);
        assert PassionMcuProtocol.decode(max, BOOT).generation == 0xffffffffL;
        System.out.println(
                "PASS typed partial projection and caller/owner/boot/range/shape guards");
    }
}
