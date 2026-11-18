package com.voyemu.vehicle;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Map;

/** Closed, partial MCU projection; callers validate the full source model before extracting SOC. */
public final class PassionMcuProtocol {
    public final String boot, sourceSha256;
    public final long generation;
    public final int soc;

    private PassionMcuProtocol(String boot, long generation, int soc, String sourceSha256) {
        this.boot = boot;
        this.generation = generation;
        this.soc = soc;
        this.sourceSha256 = sourceSha256;
    }

    public static void authorize(
            int caller, boolean dumpPermission, int serviceUid, String servicePackage) {
        if ((caller != 0 && caller != 2000)
                || !dumpPermission
                || serviceUid != 1000
                || !"com.mega.car".equals(servicePackage)) {
            throw new SecurityException(
                    "PASSION_MCU: root/shell DUMP and original system-UID Monza owner required");
        }
    }

    public static PassionMcuProtocol decode(Map<String, Object> input, String actualBoot) {
        if (input == null
                || !input.keySet()
                        .equals(
                                new HashSet<String>(
                                        Arrays.asList(
                                                "schema",
                                                "boot",
                                                "sequence",
                                                "soc",
                                                "source_sha256")))
                || !(input.get("schema") instanceof Integer)
                || ((Integer) input.get("schema")) != 1
                || !(input.get("boot") instanceof String)
                || !actualBoot.equals(input.get("boot"))
                || !(input.get("sequence") instanceof Long)
                || !(input.get("soc") instanceof Integer)
                || !(input.get("source_sha256") instanceof String)) {
            throw new IllegalArgumentException("PASSION_MCU: exact typed projection required");
        }

        long sequence = (Long) input.get("sequence");
        int soc = (Integer) input.get("soc");
        String sha = (String) input.get("source_sha256");

        if (!actualBoot.matches("[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}")
                || sequence < 1
                || sequence > 0xffffffffL
                || soc < 0
                || soc > 100
                || sha.length() != 64
                || !sha.matches("[0-9a-f]{64}")) {
            throw new IllegalArgumentException("PASSION_MCU: projection value out of range");
        }

        return new PassionMcuProtocol(actualBoot, sequence, soc, sha);
    }
}
