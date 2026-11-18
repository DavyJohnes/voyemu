package com.qinggan.canbus.service;

import org.json.JSONObject;

import java.nio.file.Files;
import java.nio.file.Path;

/** Executes actual request validation and the not-ready RegistrationService producer only. */
public final class CanObservationProducerProbe {
    private static void check(boolean condition) {
        if (!condition) {
            throw new AssertionError();
        }
    }

    public static void main(String[] args) throws Exception {
        check(new JSONObject("{\"schema\":1}").get("schema") instanceof Integer);

        for (String fractional : new String[] {"1.0", "1.9"}) {
            check(
                    !(new JSONObject("{\"schema\":" + fractional + "}").get("schema")
                            instanceof Integer));
        }

        check(new JSONObject("{\"schema\":4294967297}").get("schema") instanceof Long);
        String[] invalid = {
            "{}",
            "{\"schema\":2}",
            "{\"schema\":0}",
            "{\"schema\":1.0}",
            "{\"schema\":1.9}",
            "{\"schema\":4294967297}",
            "{\"schema\":\"1\"}",
            "{\"schema\":true}",
            "{\"schema\":false}",
            "{\"schema\":null}",
            "{\"schema\":1,\"raw\":0}",
            "{\"schema\":1,\"limit\":33}",
            "{\"schema\":1,\"states\":[\"UNKNOWN\"]}",
            "{\"schema\":1,\"states\":[\"IVI_SOC_MODESET\",\"IVI_SOC_MODESET\"]}",
            "{\"schema\":1,\"cursor\":{\"source_instance\":\"x\",\"after_seq\":0}}"
        };

        for (String bad : invalid) {
            boolean rejected = false;

            try {
                RegistrationService.currentObservation(bad);
            } catch (IllegalArgumentException | org.json.JSONException expected) {
                rejected = true;
            }

            check(rejected);
        }

        CanObservationRequest parsed =
                CanObservationRequest.parse(
                        "{\"schema\":1,\"limit\":1,\"cursor\":{\"source_instance\":\"service\",\"after_seq\":\"9223372036854775807\"}}");
        check(parsed.limit == 1 && parsed.after == Long.MAX_VALUE && parsed.states.size() == 2);
        JSONObject produced = RegistrationService.currentObservation("{\"schema\":1,\"limit\":1}");
        check(produced.length() == 3 && produced.getInt("schema") == 1);
        check("not-ready".equals(produced.getString("status")));
        check("android:qg.canbus".equals(produced.getString("source")));
        Files.writeString(Path.of(args[0]), produced.toString());
        System.out.println(
                "actual request/not-ready producer: " + invalid.length + " negatives passed");
    }
}
