package com.voyemu.input;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;

import org.json.JSONObject;

import java.io.FileInputStream;
import java.nio.charset.StandardCharsets;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Authenticated requests to the genuine OEM engine; no synthetic callback or visible-effect ACK.
 */
public final class FreeKeyInputProvider extends ContentProvider {
    private SharedPreferences storage;
    private FreeKeyConnection connection;
    private FreeKeyInputSession session;
    private String boot;

    @Override
    public boolean onCreate() {
        storage = getContext().getSharedPreferences("free-key-input", 0);
        connection = new FreeKeyConnection(getContext());

        try (FileInputStream input = new FileInputStream("/proc/sys/kernel/random/boot_id")) {
            byte[] bytes = new byte[38];
            int count = input.read(bytes);

            if (count != 37 || input.read() != -1) {
                throw new IllegalStateException("free-wheel-boot-length");
            }

            boot = new String(bytes, 0, count, StandardCharsets.US_ASCII).trim();

            if (!boot.matches("[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}")) {
                throw new IllegalStateException("free-wheel-boot");
            }
        } catch (Exception unavailable) {
            // checkstyle-allow IllegalCatch: unreadable or invalid boot identity prevents provider
            // startup
            return false;
        }

        return true;
    }

    private static int schema(Bundle extras) {
        return extras != null && Integer.valueOf(1).equals(extras.get("schema")) ? 1 : -1;
    }

    private Map<String, String> savedHolds() throws Exception {
        JSONObject object = new JSONObject(storage.getString("holds", "{}"));
        Map<String, String> values = new LinkedHashMap<>();

        for (Iterator<String> keys = object.keys(); keys.hasNext(); ) {
            String key = keys.next();
            values.put(key, object.getString(key));
        }

        return values;
    }

    private void load() throws Exception {
        if (session == null) {
            session =
                    new FreeKeyInputSession(
                            storage.getString("session", null),
                            storage.getString("token", null),
                            storage.getLong("sequence", 0),
                            boot.equals(storage.getString("boot", null))
                                    ? savedHolds()
                                    : new LinkedHashMap<>(),
                            boot.equals(storage.getString("boot", null))
                                    && storage.getBoolean("pending_submission", false));
        }
    }

    private void authenticate(Bundle extras) throws Exception {
        load();

        if (!boot.equals(storage.getString("boot", null))
                || !session.authenticates(
                        schema(extras), extras.getString("session"), extras.getString("token"))) {
            throw new SecurityException("free-wheel-authentication");
        }
    }

    private void validateRotation(boolean same, String id, String token) throws Exception {
        if (storage.contains("session")) {
            load();

            if (boot.equals(storage.getString("boot", null)) && session.pendingSubmission()) {
                throw new IllegalStateException("free-wheel-pending-submission");
            }

            if (same && !session.authenticates(1, id, token)) {
                throw new SecurityException("free-wheel-token-change");
            }

            if (!same && boot.equals(storage.getString("boot", null)) && session.hasHolds()) {
                throw new IllegalArgumentException("free-wheel-release-before-rotation");
            }
        }
    }

    private JSONObject bootstrap(Bundle extras) throws Exception {
        if (schema(extras) != 1) {
            throw new IllegalArgumentException("free-wheel-schema");
        }

        String id = extras.getString("session"), token = extras.getString("token");
        // The first construction validates the session id and token before any side effect.
        @SuppressWarnings("UnusedVariable")
        FreeKeyInputSession candidate =
                new FreeKeyInputSession(id, token, 0, new LinkedHashMap<>());
        boolean same = id.equals(storage.getString("session", null));
        validateRotation(same, id, token);
        connection.ensure();

        if (!same || !boot.equals(storage.getString("boot", null))) {
            FreeStartupAudioPolicy.zero(getContext());
        }

        long sequence = same ? session.sequence() : 0;
        Map<String, String> holds =
                same && boot.equals(storage.getString("boot", null))
                        ? savedHolds()
                        : new LinkedHashMap<>();
        candidate = new FreeKeyInputSession(id, token, sequence, holds);
        JSONObject receipt =
                new JSONObject()
                        .put("schema", 1)
                        .put("session_id", id)
                        .put("sequence", sequence)
                        .put("boot_id", boot)
                        .put("status", "session-ready")
                        .put("scope", "genuine-oem-binder-bound; effects-unverified");
        SharedPreferences.Editor update = storage.edit();

        if (!same) {
            update.clear();
        }

        if (!update.putString("session", id)
                .putString("token", token)
                .putLong("sequence", sequence)
                .putString("holds", new JSONObject(holds).toString())
                .putBoolean("pending_submission", false)
                .putString("boot", boot)
                .putString("bootstrap", receipt.toString())
                .commit()) {
            throw new IllegalStateException("free-wheel-storage");
        }

        session = candidate;

        return receipt;
    }

    private JSONObject presave(Bundle extras) throws Exception {
        authenticate(extras);
        String nonce = extras.getString("save_nonce");

        if (nonce == null || !nonce.matches("[0-9a-f]{32}")) {
            throw new IllegalArgumentException("free-wheel-save-nonce");
        }

        if (session.pendingSubmission() || session.hasHolds()) {
            throw new IllegalStateException("free-wheel-save-held-input");
        }

        connection.ensure();
        FreeStartupAudioPolicy.zero(getContext());

        return new JSONObject()
                .put("schema", 1)
                .put("status", "presave-ready")
                .put("session_id", session.session)
                .put("boot_id", boot)
                .put("sequence", session.sequence())
                .put("save_nonce", nonce)
                .put("acoustic_zero", true);
    }

    private JSONObject input(Bundle extras) throws Exception {
        authenticate(extras);

        if (!(extras.get("sequence") instanceof Long)) {
            throw new IllegalArgumentException("free-wheel-sequence");
        }

        long next = (Long) extras.get("sequence");
        String button = extras.getString("button"), phase = extras.getString("phase");
        connection.ensure();
        JSONObject pending =
                new JSONObject()
                        .put("schema", 1)
                        .put("session_id", session.session)
                        .put("boot_id", boot)
                        .put("sequence", next)
                        .put("button", button)
                        .put("phase", phase)
                        .put("status", "terminal-unavailable")
                        .put("oem_handled", JSONObject.NULL)
                        .put("visible_effect_verified", false)
                        .put("submission_confirmed", false);
        FreeKeyInputSession.Event event =
                session.accept(
                        next,
                        button,
                        phase,
                        extras.getString("owner"),
                        (value, holds) -> {
                            if (!storage.edit()
                                    .putLong("sequence", value)
                                    .putString("holds", new JSONObject(holds).toString())
                                    .putBoolean("pending_submission", true)
                                    .putString("input", pending.toString())
                                    .commit()) {
                                throw new IllegalStateException("free-wheel-storage");
                            }
                        });
        connection.submit(event);
        JSONObject receipt =
                pending.put("status", "terminal-requested")
                        .put("submission_confirmed", true)
                        .put("key_code", event.key)
                        .put("action", event.action)
                        .put("flags", event.flags)
                        .put("scope", "OEM transaction7 completed; callbacks/effects not inferred");
        session.confirmed(
                event,
                (value, holds) -> {
                    if (!storage.edit()
                            .putLong("sequence", value)
                            .putString("holds", new JSONObject(holds).toString())
                            .putBoolean("pending_submission", false)
                            .putString("input", receipt.toString())
                            .commit()) {
                        throw new IllegalStateException("free-wheel-receipt");
                    }
                });

        return receipt;
    }

    @Override
    public synchronized Bundle call(String method, String arg, Bundle extras) {
        if (!FreeKeyInputSession.callerAllowed(
                Binder.getCallingUid(),
                getContext().checkCallingPermission("android.permission.DUMP")
                        == PackageManager.PERMISSION_GRANTED)) {
            throw new SecurityException("free-wheel-caller");
        }

        long identity = Binder.clearCallingIdentity();

        try {
            if (boot == null) {
                throw new IllegalStateException("free-wheel-boot-unavailable");
            }

            JSONObject receipt;

            if ("bootstrap".equals(method)) {
                receipt = bootstrap(extras);
            } else if ("input".equals(method)) {
                receipt = input(extras);
            } else if ("presave".equals(method)) {
                receipt = presave(extras);
            } else if ("status".equals(method)) {
                authenticate(extras);

                if (!"bootstrap".equals(arg) && !"input".equals(arg)) {
                    throw new IllegalArgumentException("free-wheel-status");
                }

                String text = storage.getString(arg, null);

                if (text == null || text.length() > 8192) {
                    throw new IllegalStateException("free-wheel-no-receipt");
                }

                receipt = new JSONObject(text);

                if (!session.session.equals(receipt.getString("session_id"))
                        || !boot.equals(receipt.getString("boot_id"))) {
                    throw new IllegalStateException("free-wheel-stale-receipt");
                }
            } else {
                throw new IllegalArgumentException("free-wheel-method");
            }

            Bundle output = new Bundle();
            output.putString("json", receipt.toString());

            return output;
        } catch (Exception refused) {
            // checkstyle-allow IllegalCatch: any failed authenticated request maps to its existing
            // error reply
            Bundle output = new Bundle();
            output.putString(
                    "error",
                    refused instanceof SecurityException
                            ? "authentication"
                            : refused instanceof IllegalArgumentException
                                    ? "invalid-command"
                                    : "storage-or-submission");

            return output;
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
    public int delete(Uri uri, String where, String[] args) {
        throw new UnsupportedOperationException("wheel-call-only");
    }

    @Override
    public int update(Uri uri, ContentValues values, String where, String[] args) {
        throw new UnsupportedOperationException("wheel-call-only");
    }
}
