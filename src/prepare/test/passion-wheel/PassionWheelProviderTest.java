package com.voyemu.input;

import android.content.Context;
import android.os.Bundle;

import org.json.JSONObject;

import java.lang.reflect.Field;

/** Exercises the real provider bootstrap boundary with in-memory Android services. */
public final class PassionWheelProviderTest {
    private static final String BOOT = "12345678-1234-1234-1234-123456789abc";
    private static final String TOKEN = "0123456789abcdef0123456789abcdef";
    private static final String ID = "session_a";

    private static final class Storage extends Context.MemoryPreferences {
        int edits;

        @Override
        public android.content.SharedPreferences.Editor edit() {
            edits++;

            return super.edit();
        }
    }

    private static final class Fixture {
        final PassionWheelProvider provider = new PassionWheelProvider();
        final Storage storage = new Storage();

        Fixture(boolean existing, boolean currentBoot, boolean pending, boolean held)
                throws Exception {
            set("boot", BOOT);
            set("storage", storage);

            if (existing) {
                storage.values.put("session", ID);
                storage.values.put("token", TOKEN);
                storage.values.put("boot", currentBoot ? BOOT : "previous-boot");
                storage.values.put("sequence", 7L);
                storage.values.put("pending_submission", pending);
                storage.values.put("holds", held ? "{\"next\":\"owner\"}" : "{}");
            }

            provider.context.audio.levels[3] = 6;
        }

        void set(String name, Object value) throws Exception {
            Field field = PassionWheelProvider.class.getDeclaredField(name);

            field.setAccessible(true);
            field.set(provider, value);
        }

        Bundle bootstrap(String id, String token) {
            Bundle extras = new Bundle();

            extras.putInt("schema", 1);
            extras.putString("session", id);
            extras.putString("token", token);

            return provider.call("bootstrap", null, extras);
        }

        void refused(String id, String token, String error) {
            need(error.equals(bootstrap(id, token).getString("error")), "refusal classification");
            need(storage.edits == 0, "validation precedes storage");
            need(provider.context.audio.levels[3] == 6, "validation precedes audio reset");
        }

        void ready(String id, long sequence, boolean reset) {
            JSONObject receipt = new JSONObject(bootstrap(id, TOKEN).getString("json"));

            need(id.equals(receipt.getString("session_id")), "receipt session");
            need(((Number) receipt.get("sequence")).longValue() == sequence, "sequence retained");
            need(storage.edits == 1, "one bootstrap persistence");
            need(provider.context.audio.levels[3] == (reset ? 0 : 6), "audio reset gate");
            need(Boolean.FALSE.equals(storage.values.get("pending_submission")), "pending reset");
            need(BOOT.equals(storage.values.get("boot")), "boot persisted");
        }
    }

    private static void need(boolean value, String reason) {
        if (!value) {
            throw new AssertionError(reason);
        }
    }

    public static void main(String[] args) throws Exception {
        new Fixture(false, true, false, false).refused("bad", TOKEN, "invalid-command");
        new Fixture(true, true, true, false)
                .refused(ID, "ffffffffffffffffffffffffffffffff", "storage-or-submission");
        new Fixture(true, true, false, false)
                .refused(ID, "ffffffffffffffffffffffffffffffff", "authentication");
        new Fixture(true, true, false, true).refused("session_b", TOKEN, "invalid-command");
        new Fixture(true, true, true, false).refused("session_b", TOKEN, "storage-or-submission");
        new Fixture(false, true, false, false).ready(ID, 0, true);
        new Fixture(true, true, false, true).ready(ID, 7, false);
        new Fixture(true, true, false, false).ready("session_b", 0, true);
        new Fixture(true, false, true, true).ready(ID, 7, true);
        new Fixture(true, false, true, true).ready("session_b", 0, true);
        System.out.println(
                "Passion provider bootstrap validation, rotation, cold boot and side-effect order"
                        + " PASS");
    }
}
