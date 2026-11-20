package com.voyemu.input;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;
import android.os.SystemClock;

import com.voyemu.oem.FreeKeyService;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.Map;

/** DUMP-protected host endpoint; only root/shell may control this own input component. */
public final class FreeWheelProvider extends ContentProvider {
    private SharedPreferences storage;
    private FreeWheelPhases phases;

    @Override
    public boolean onCreate() {
        storage = getContext().getSharedPreferences("free-wheel", 0);

        return true;
    }

    private static int schema(Bundle extras) {
        Object value = extras == null ? null : extras.get("schema");

        return value instanceof Integer && ((Integer) value) == 1 ? 1 : -1;
    }

    private void load() throws Exception {
        if (phases != null) {
            return;
        }

        FreeWheelPhases loaded =
                new FreeWheelPhases(
                        storage.getString("session", null),
                        storage.getString("token", null),
                        storage.getLong("sequence", 0));
        JSONObject saved = new JSONObject(storage.getString("held", "{}"));
        Map<String, String> restored = new LinkedHashMap<>();

        for (Iterator<String> keys = saved.keys(); keys.hasNext(); ) {
            String key = keys.next();
            restored.put(key, saved.getString(key));
        }

        loaded.restore(restored);
        phases = loaded;
    }

    private void authenticate(Bundle extras) throws Exception {
        load();

        if (!phases.authenticates(
                schema(extras), extras.getString("session"), extras.getString("token"))) {
            throw new SecurityException("wheel-authentication");
        }
    }

    private JSONObject bootstrap(Bundle extras) throws Exception {
        if (schema(extras) != 1) {
            throw new IllegalArgumentException("invalid-schema");
        }

        String session = extras.getString("session"), token = extras.getString("token");
        FreeWheelPhases candidate = new FreeWheelPhases(session, token, 0);
        String previous = storage.getString("session", null);

        if (session.equals(previous)) {
            authenticate(extras);
        } else {
            loadIfPresent();

            if (phases != null && phases.heldCount() != 0) {
                throw new IllegalArgumentException("release-before-session-change");
            }

            JSONObject receipt =
                    new JSONObject()
                            .put("schema", 1)
                            .put("session_id", session)
                            .put("sequence", 0)
                            .put("status", "session-ready")
                            .put("listener_count", FreeKeyService.BROKER.size());

            if (!storage.edit()
                    .clear()
                    .putString("session", session)
                    .putString("token", token)
                    .putLong("sequence", 0)
                    .putString("held", "{}")
                    .putString("bootstrap", receipt.toString())
                    .commit()) {
                throw new IllegalStateException("wheel-storage");
            }

            phases = candidate;
        }

        return new JSONObject(storage.getString("bootstrap", "{}"));
    }

    private void loadIfPresent() throws Exception {
        if (storage.contains("session")) {
            load();
        }
    }

    private JSONObject input(Bundle extras) throws Exception {
        authenticate(extras);
        Object sequenceValue = extras.get("sequence");

        if (!(sequenceValue instanceof Long)) {
            throw new IllegalArgumentException("invalid-sequence");
        }

        long next = (Long) sequenceValue;
        String button = extras.getString("button"),
                phase = extras.getString("phase"),
                owner = extras.getString("owner");
        JSONObject pending =
                new JSONObject()
                        .put("schema", 1)
                        .put("session_id", phases.session)
                        .put("sequence", next)
                        .put("button", button)
                        .put("phase", phase)
                        .put("key_code", FreeWheelPhases.key(button))
                        .put("status", "terminal-unavailable")
                        .put("reason", "delivery-not-confirmed")
                        .put("visible_effect_verified", false);
        int[] actions =
                phases.accept(
                        next,
                        button,
                        phase,
                        owner,
                        SystemClock.elapsedRealtime(),
                        value -> {
                            Map<String, String> holds = phases.holds();

                            if ("down".equals(phase)) {
                                holds.put(button, owner);
                            } else {
                                holds.remove(button);
                            }

                            if (!storage.edit()
                                    .putLong("sequence", value)
                                    .putString("held", new JSONObject(holds).toString())
                                    .putString("input", pending.toString())
                                    .commit()) {
                                throw new IllegalStateException("wheel-storage");
                            }
                        });
        FreeWheelBroker.Result delivery = new FreeWheelBroker.Result();
        JSONArray emitted = new JSONArray();
        FreeKeyService.beginDelivery();

        try {
            for (int action : actions) {
                emitted.put(action);
                delivery.add(FreeKeyService.BROKER.dispatch(FreeWheelPhases.key(button), action));
            }
        } finally {
            FreeKeyService.endDelivery();
        }

        JSONObject receipt =
                pending.put("status", delivery.status())
                        .put("actions", emitted)
                        .put("callbacks", delivery.delivered)
                        .put("attempted_callbacks", delivery.attempted)
                        .put("failed_callbacks", delivery.failed)
                        .put("handled_callbacks", delivery.handled)
                        .put("callback_registrations", new JSONArray(delivery.registrations))
                        .put(
                                "callback_registrations_truncated",
                                delivery.delivered > delivery.registrations.size())
                        .put("consumed", delivery.handled > 0)
                        .put("terminals", 0)
                        .put("src_callback_notified", false);
        receipt.remove("reason");

        if (!storage.edit().putString("input", receipt.toString()).commit()) {
            throw new IllegalStateException("wheel-receipt-storage");
        }

        return receipt;
    }

    @Override
    public synchronized Bundle call(String method, String arg, Bundle extras) {
        if (!FreeWheelBroker.controlCallerAllowed(
                Binder.getCallingUid(),
                getContext().checkCallingPermission("android.permission.DUMP")
                        == PackageManager.PERMISSION_GRANTED)) {
            throw new SecurityException("wheel-control-caller");
        }

        long identity = Binder.clearCallingIdentity();

        try {
            JSONObject receipt;

            if ("bootstrap".equals(method)) {
                receipt = bootstrap(extras);
            } else if ("input".equals(method)) {
                receipt = input(extras);
            } else if ("status".equals(method)) {
                authenticate(extras);

                if (!"bootstrap".equals(arg) && !"input".equals(arg)) {
                    throw new IllegalArgumentException("invalid-status");
                }

                String saved = storage.getString(arg, null);

                if (saved == null || saved.length() > 16384) {
                    throw new IllegalArgumentException("receipt-unavailable");
                }

                receipt = new JSONObject(saved);

                if (!phases.session.equals(receipt.getString("session_id"))) {
                    throw new IllegalArgumentException("stale-receipt");
                }
            } else {
                throw new IllegalArgumentException("unknown-wheel-method");
            }

            Bundle result = new Bundle();
            result.putString("json", receipt.toString());

            return result;
        } catch (Exception failure) {
            // checkstyle-allow IllegalCatch: all request failures become credential-free
            // closed-vocabulary error replies
            // No credentials, raw extras, or exception messages are exposed.
            Bundle result = new Bundle();
            result.putString(
                    "error",
                    failure instanceof SecurityException
                            ? "authentication"
                            : failure instanceof IllegalArgumentException
                                    ? "invalid-command"
                                    : "storage-or-delivery");

            return result;
        } finally {
            Binder.restoreCallingIdentity(identity);
        }
    }

    @Override
    public Cursor query(
            Uri uri, String[] projection, String selection, String[] args, String sort) {
        throw new UnsupportedOperationException("wheel-call-only");
    }

    @Override
    public String getType(Uri uri) {
        return null;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        throw new UnsupportedOperationException("wheel-call-only");
    }

    @Override
    public int delete(Uri uri, String selection, String[] args) {
        throw new UnsupportedOperationException("wheel-call-only");
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] args) {
        throw new UnsupportedOperationException("wheel-call-only");
    }
}
