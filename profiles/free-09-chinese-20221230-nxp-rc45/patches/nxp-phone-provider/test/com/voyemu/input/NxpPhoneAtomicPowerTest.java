package com.voyemu.input;

import android.os.Bundle;
import android.os.Handler;
import android.os.PeerTestRuntime;

import com.qinggan.bluetooth.internal.ivt8.Ivt8BTAdapter;

import org.json.JSONObject;

/**
 * Production provider reservation plus production adapter, including snapshot-to-dispatch power
 * races.
 */
public final class NxpPhoneAtomicPowerTest {
    static final String BOOT = "00000000-0000-0000-0000-000000000001",
            TOKEN = "0123456789abcdef0123456789abcdef";
    static int checks;

    static void need(boolean value, String why) {
        checks++;

        if (!value) {
            throw new AssertionError(why);
        }
    }

    static final class Provider extends PhoneControlProvider {
        final Ivt8BTAdapter peer;
        int calls;
        boolean race, timeout;
        JSONObject replacement;

        Provider(Ivt8BTAdapter peer) {
            this.peer = peer;
        }

        @Override
        protected String readBoot() {
            return BOOT;
        }

        @Override
        protected JSONObject adapter(String method, String payload) throws Exception {
            calls++;

            if ("connect".equals(method)) {
                if (race) {
                    race = false;
                    peer.setPowerOnHandler(false);
                }

                if (timeout) {
                    throw new java.util.concurrent.TimeoutException("not a receipt");
                }

                if (replacement != null) {
                    return replacement;
                }
            }

            return new JSONObject(peer.control(method, payload));
        }
    }

    static Bundle request(long sequence) {
        Bundle b = new Bundle();

        b.putInt("schema", 1);
        b.putString("session", "owned");
        b.putString("token", TOKEN);
        b.putString("boot", BOOT);
        b.putLong("sequence", sequence);

        return b;
    }

    static JSONObject call(Provider p, String operation, long sequence) throws Exception {
        Bundle b = request(sequence);

        if ("bootstrap".equals(operation)) {
            b.putBoolean("reset", false);
        }

        if ("control".equals(operation)) {
            b.putString("method", "connect");
            b.putString("payload", "{}");
        }

        return new JSONObject(p.call(operation, null, b).getString("json"));
    }

    static void cleanOff(JSONObject reply) {
        need(Boolean.FALSE.equals(reply.get("ok")), "must reject");
        need("adapter_precondition".equals(reply.get("stage")), "closed stage");
        need("PHONE_POWER_OFF".equals(reply.get("reason")), "closed reason");
        need(Boolean.FALSE.equals(reply.get("mutation_started")), "certified no mutation");
    }

    public static void main(String[] args) throws Exception {
        System.setProperty("phone.peer.test.files", args[0]);
        PeerTestRuntime.enter("phone");
        Handler.immediate = true;
        Ivt8BTAdapter peer = Ivt8BTAdapter.getInstance(new android.content.Context(), null);
        PeerTestRuntime.install(peer);
        PeerTestRuntime.CALLER.set(new PeerTestRuntime.Identity(2000, 9000, "shell"));
        Provider p = new Provider(peer);
        need(call(p, "bootstrap", 0).getBoolean("ok"), "bootstrap");
        JSONObject before = call(p, "snapshot", 0);
        need(before.getJSONObject("data").getBoolean("powered"), "host sees ON");
        p.race = true;
        cleanOff(call(p, "control", 1));
        JSONObject off = call(p, "snapshot", 0);
        need(
                !off.getJSONObject("data").getBoolean("powered")
                        && !off.getJSONObject("data").getBoolean("connected"),
                "OFF no mutation, old sequence reusable");
        peer.setPowerOnHandler(true);
        JSONObject connected = call(p, "control", 1);
        need(
                connected.getBoolean("ok")
                        && connected.getJSONObject("data").getBoolean("connected")
                        && connected.getLong("sequence") == 1,
                "same boot connects after ON using same sequence");
        peer.control("disconnect", "{}");
        peer.setPowerOnHandler(false);
        cleanOff(call(p, "control", 2));
        peer.setPowerOnHandler(true);
        need(call(p, "control", 2).getBoolean("ok"), "second OFF did not poison");
        peer.control("disconnect", "{}");

        for (JSONObject bad :
                new JSONObject[] {
                    new JSONObject().put("ok", false).put("error", "PHONE_POWER_OFF"),
                    new JSONObject()
                            .put("ok", false)
                            .put("error", "PHONE_POWER_OFF")
                            .put("mutation_started", true),
                    new JSONObject()
                            .put("ok", false)
                            .put("error", "PHONE_POWER_OFF")
                            .put("mutation_started", false)
                            .put("extra", true)
                }) {
            Provider malformed = new Provider(peer);
            need(call(malformed, "bootstrap", 0).getBoolean("ok"), "malformed bootstrap");
            malformed.replacement = bad;
            JSONObject failure = call(malformed, "control", 1);
            need(
                    !failure.getBoolean("ok") && failure.getBoolean("mutation_started"),
                    "malformed refusal remains uncertain");
            int calls = malformed.calls;
            need(
                    !call(malformed, "control", 2).getBoolean("ok") && calls == malformed.calls,
                    "uncertain result cannot replay");
        }

        Provider timed = new Provider(peer);
        need(call(timed, "bootstrap", 0).getBoolean("ok"), "timeout bootstrap");
        timed.timeout = true;
        JSONObject timeout = call(timed, "control", 1);
        need(
                "PHONE_ADAPTER_TIMEOUT".equals(timeout.get("reason"))
                        && timeout.getBoolean("mutation_started"),
                "timeout is never a clean OFF refusal");
        int calls = timed.calls;
        timed.timeout = false;
        need(
                !call(timed, "control", 2).getBoolean("ok") && calls == timed.calls,
                "late recovery cannot replay unknown mutation");
        System.out.println(
                "NXP atomic power PASS "
                        + checks
                        + " production provider+adapter: snapshot ON / handler OFF race, sequence"
                        + " rollback, ON reconnect, malformed and timeout quarantine");
    }
}
