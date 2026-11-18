package com.voyemu.input;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;

import org.json.JSONObject;

import java.io.FileInputStream;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;

/** Shell-only control of the adapter already owned by the OEM Phone process. */
public class PhoneControlProvider extends ContentProvider {
    private String ownerSession, ownerToken, ownerBoot;
    private long sequence;
    private boolean poisoned;
    private String diagnosticStage = "request";

    @Override
    public boolean onCreate() {
        return true;
    }

    /** Diagnostic state of one control request, reported only after authentication. */
    private static final class Attempt {
        boolean authenticated;
        boolean mutationStarted;
    }

    @Override
    public Bundle call(String operation, String argument, Bundle extras) {
        int uid = Binder.getCallingUid();

        if ((uid != 0 && uid != 2000)
                || getContext().checkCallingPermission("android.permission.DUMP")
                        != PackageManager.PERMISSION_GRANTED) {
            throw new SecurityException("PHONE_PERMISSION");
        }

        synchronized (this) {
            Attempt attempt = new Attempt();
            diagnosticStage = "request";

            try {
                return control(operation, argument, extras, attempt);
            } catch (Exception error) {
                // checkstyle-allow IllegalCatch: each rejected request is a closed-vocabulary reply
                return rejected(error, attempt);
            }
        }
    }

    private Bundle control(String operation, String argument, Bundle extras, Attempt attempt)
            throws Exception {
        requireRequest(operation, argument, extras);
        String session = extras.getString("session"),
                token = extras.getString("token"),
                boot = extras.getString("boot");
        long requested = extras.getLong("sequence", -1);
        diagnosticStage = "identity";
        requireIdentity(session, token, boot);

        if ("bootstrap".equals(operation)) {
            return bootstrap(extras.get("reset"), session, token, boot, requested, attempt);
        }

        if (!session.equals(ownerSession) || !token.equals(ownerToken) || !boot.equals(ownerBoot)) {
            throw new IllegalStateException("PHONE_OWNER");
        }

        attempt.authenticated = true;

        if ("snapshot".equals(operation)) {
            if (requested != sequence) {
                throw new IllegalStateException("PHONE_SEQUENCE");
            }

            return reply(session, boot, adapter("snapshot", "{}"));
        }

        if (poisoned) {
            throw new IllegalStateException("PHONE_UNCERTAIN");
        }

        diagnosticStage = "request";
        String method = extras.getString("method"), payload = extras.getString("payload");
        requireControl(operation, method, payload, requested);
        new JSONObject(payload);
        // Reserve before the callback: a timed-out operation must never be replayed.
        sequence = requested;
        attempt.mutationStarted = true;
        boolean completed = false;

        try {
            Bundle result = reply(session, boot, adapter(method, payload));
            completed = true;

            return result;
        } finally {
            if (!completed) {
                poisoned = true;
            }
        }
    }

    private static void requireRequest(String operation, String argument, Bundle extras) {
        if (extras == null || argument != null) {
            throw new IllegalArgumentException("PHONE_REQUEST");
        }

        java.util.Set<String> keys =
                new java.util.HashSet<String>(
                        java.util.Arrays.asList("schema", "session", "token", "boot", "sequence"));

        if ("control".equals(operation)) {
            keys.add("method");
            keys.add("payload");
        } else if ("bootstrap".equals(operation)) {
            keys.add("reset");
        } else if (!"snapshot".equals(operation)) {
            throw new IllegalArgumentException("PHONE_REQUEST");
        }

        if (!keys.equals(extras.keySet())
                || !(Integer.valueOf(1).equals(extras.get("schema"))
                        || Long.valueOf(1).equals(extras.get("schema")))
                || !(extras.get("sequence") instanceof Long)
                || !(extras.get("session") instanceof String)
                || !(extras.get("token") instanceof String)
                || !(extras.get("boot") instanceof String)
                || ("bootstrap".equals(operation) && !(extras.get("reset") instanceof Boolean))
                || ("control".equals(operation)
                        && (!(extras.get("method") instanceof String)
                                || !(extras.get("payload") instanceof String)))) {
            throw new IllegalArgumentException("PHONE_REQUEST");
        }
    }

    private void requireIdentity(String session, String token, String boot) throws Exception {
        if (session == null
                || !session.matches("[a-zA-Z0-9_-]{1,128}")
                || token == null
                || !token.matches("[0-9a-f]{32}")
                || boot == null
                || !boot.equals(readBoot())) {
            throw new IllegalArgumentException("PHONE_IDENTITY");
        }
    }

    private void requireControl(String operation, String method, String payload, long requested) {
        if (!"control".equals(operation)
                || method == null
                || !method.matches("configure|connect|disconnect|incoming|answer|hangup")
                || payload == null
                || payload.length() > 4096
                || requested != sequence + 1) {
            throw new IllegalArgumentException("PHONE_REQUEST");
        }
    }

    private Bundle bootstrap(
            Object reset,
            String session,
            String token,
            String boot,
            long requested,
            Attempt attempt)
            throws Exception {
        diagnosticStage = "bootstrap";

        if (requested != 0) {
            throw new IllegalStateException("PHONE_SEQUENCE");
        }

        boolean same =
                session.equals(ownerSession) && token.equals(ownerToken) && boot.equals(ownerBoot);

        if (same && (sequence != 0 || poisoned)) {
            throw new IllegalStateException("PHONE_OWNER");
        }

        JSONObject state;

        if (ownerSession != null && !same) {
            state = resetOwner(reset, attempt);
        } else {
            attempt.authenticated = true;
            state = adapter("snapshot", "{}");
        }

        if (!Boolean.TRUE.equals(state.opt("ok"))) {
            throw new IllegalStateException("PHONE_ADAPTER_REJECTED");
        }

        ownerSession = session;
        ownerToken = token;
        ownerBoot = boot;
        sequence = 0;
        poisoned = false;

        return reply(session, boot, state);
    }

    /** Takes over from another owner after disconnecting an idle peer; returns its state. */
    private JSONObject resetOwner(Object reset, Attempt attempt) throws Exception {
        if (!Boolean.TRUE.equals(reset)) {
            throw new IllegalStateException("PHONE_OWNER");
        }

        attempt.authenticated = true;
        JSONObject state = adapter("snapshot", "{}");
        diagnosticStage = "bootstrap";

        if (!idle(state)) {
            throw new IllegalStateException("PHONE_ACTIVE_CALL");
        }

        if (Boolean.TRUE.equals(state.opt("connected"))
                || Boolean.TRUE.equals(state.opt("pbap_connected"))) {
            boolean completed = false;

            try {
                attempt.mutationStarted = true;
                state = adapter("disconnect", "{}");

                if (!idle(state)
                        || !Boolean.FALSE.equals(state.opt("connected"))
                        || !Boolean.FALSE.equals(state.opt("pbap_connected"))) {
                    throw new IllegalStateException("PHONE_RESET_UNCERTAIN");
                }

                completed = true;
            } finally {
                if (!completed) {
                    poisoned = true;
                }
            }
        }

        if (!Boolean.FALSE.equals(state.opt("connected"))
                || !Boolean.FALSE.equals(state.opt("pbap_connected"))) {
            throw new IllegalStateException("PHONE_RESET_UNCERTAIN");
        }

        return state;
    }

    private Bundle rejected(Exception error, Attempt attempt) {
        Bundle result = new Bundle();
        String json = "{\"schema\":1,\"ok\":false,\"error\":\"PHONE_CONTROL_REJECTED\"}";

        if (attempt.authenticated) {
            // Closed vocabulary only: never return exception text, class names or credentials.
            json =
                    json.substring(0, json.length() - 1)
                            + ",\"stage\":\""
                            + diagnosticStage
                            + "\",\"reason\":\""
                            + reason(error)
                            + "\",\"mutation_started\":"
                            + attempt.mutationStarted
                            + "}";
        }

        result.putString("json", json);

        return result;
    }

    protected JSONObject adapter(String method, String payload) throws Exception {
        diagnosticStage = "adapter_lookup";
        Object adapter = adapterOnMain();
        // The factory's external PathClassLoader owns this object's runtime class.
        Method control = adapter.getClass().getMethod("control", String.class, String.class);

        try {
            diagnosticStage = "adapter_control";
            Object output = control.invoke(adapter, method, payload);
            diagnosticStage = "reply";

            if (!(output instanceof String) || ((String) output).length() > 65536) {
                throw new IllegalStateException("PHONE_ADAPTER_RESPONSE");
            }

            return new JSONObject((String) output);
        } catch (InvocationTargetException error) {
            throw new IllegalStateException("PHONE_ADAPTER_REJECTED", error.getCause());
        }
    }

    private Object lookupAdapter() throws Exception {
        Class<?> managerType =
                Class.forName(
                        "com.qinggan.bluetooth.BTManager", true, getContext().getClassLoader());
        Object manager =
                managerType
                        .getMethod("getInstance", android.content.Context.class)
                        .invoke(null, getContext());
        Field field = managerType.getDeclaredField("mAdapter");

        field.setAccessible(true);
        Object adapter = field.get(manager);

        if (adapter == null) {
            throw new IllegalStateException("PHONE_ADAPTER_FRESH_PROCESS_REQUIRED");
        }

        return adapter;
    }

    private Object adapterOnMain() throws Exception {
        android.os.Looper main = getContext().getMainLooper();

        if (android.os.Looper.myLooper() == main) {
            try {
                return lookupAdapter();
            } catch (Error error) {
                throw new IllegalStateException("PHONE_ADAPTER_INIT", error);
            }
        }

        java.util.concurrent.FutureTask<Object> lookup =
                new java.util.concurrent.FutureTask<Object>(() -> lookupAdapter());

        if (!new android.os.Handler(main).post(lookup)) {
            throw new IllegalStateException("PHONE_ADAPTER_INIT");
        }

        try {
            return lookup.get(1000, java.util.concurrent.TimeUnit.MILLISECONDS);
        } catch (java.util.concurrent.TimeoutException error) {
            lookup.cancel(false);
            throw new IllegalStateException("PHONE_ADAPTER_TIMEOUT", error);
        } catch (InterruptedException error) {
            lookup.cancel(false);
            Thread.currentThread().interrupt();
            throw new IllegalStateException("PHONE_ADAPTER_INIT", error);
        } catch (java.util.concurrent.ExecutionException error) {
            Throwable cause = error.getCause();

            if (cause instanceof Exception) {
                throw (Exception) cause;
            }

            throw new IllegalStateException("PHONE_ADAPTER_INIT", cause);
        }
    }

    private static String reason(Exception error) {
        Throwable cause = error;

        for (int depth = 0; depth < 8 && cause != null; depth++, cause = cause.getCause()) {
            if (cause instanceof java.util.concurrent.TimeoutException
                    || "PHONE_TIMEOUT_OUTCOME_UNKNOWN".equals(cause.getMessage())) {
                return "PHONE_ADAPTER_TIMEOUT";
            }
        }

        if (error instanceof ClassNotFoundException) {
            return "PHONE_ADAPTER_CLASS";
        }

        if (error instanceof NoSuchFieldException) {
            return "PHONE_ADAPTER_FIELD";
        }

        if (error instanceof NoSuchMethodException) {
            return "PHONE_ADAPTER_METHOD";
        }

        if (error instanceof IllegalAccessException || error instanceof SecurityException) {
            return "PHONE_ADAPTER_ACCESS";
        }

        if (error instanceof InvocationTargetException) {
            return "PHONE_ADAPTER_INIT";
        }

        if (error instanceof java.io.IOException) {
            return "PHONE_BOOT_IO";
        }

        return messageReason(error.getMessage());
    }

    private static String messageReason(String message) {
        if (message == null) {
            return "PHONE_INTERNAL";
        }

        switch (message) {
            case "PHONE_REQUEST":
                return "PHONE_REQUEST";
            case "PHONE_OWNER":
                return "PHONE_OWNER";
            case "PHONE_SEQUENCE":
                return "PHONE_SEQUENCE";
            case "PHONE_ACTIVE_CALL":
                return "PHONE_ACTIVE_CALL";
            case "PHONE_ADAPTER_UNAVAILABLE":
                return "PHONE_ADAPTER_UNAVAILABLE";
            case "PHONE_ADAPTER_FRESH_PROCESS_REQUIRED":
                return "PHONE_ADAPTER_FRESH_PROCESS_REQUIRED";
            case "PHONE_ADAPTER_INIT":
                return "PHONE_ADAPTER_INIT";
            case "PHONE_ADAPTER_REJECTED":
                return "PHONE_ADAPTER_CONTROL";
            case "PHONE_ADAPTER_RESPONSE":
                return "PHONE_ADAPTER_RESPONSE";
            case "PHONE_UNCERTAIN":
            case "PHONE_RESET_UNCERTAIN":
                return "PHONE_UNCERTAIN";
            default:
                return "PHONE_INTERNAL";
        }
    }

    private static boolean idle(JSONObject state) {
        Object call = state.opt("call");

        return call instanceof JSONObject
                && Integer.valueOf(131).equals(((JSONObject) call).opt("state"));
    }

    private Bundle reply(String session, String boot, JSONObject data) throws Exception {
        diagnosticStage = "reply";

        if (!Boolean.TRUE.equals(data.opt("ok"))) {
            throw new IllegalStateException("PHONE_ADAPTER_REJECTED");
        }

        JSONObject json = new JSONObject();
        json.put("schema", 1);
        json.put("ok", true);
        json.put("session", session);
        json.put("boot_id", boot);
        json.put("sequence", sequence);
        json.put("provider_pid", android.os.Process.myPid());
        json.put("data", data);
        Bundle result = new Bundle();
        result.putString("json", json.toString());

        return result;
    }

    protected String readBoot() throws Exception {
        try (FileInputStream stream = new FileInputStream("/proc/sys/kernel/random/boot_id")) {
            byte[] bytes = new byte[64];
            int count = stream.read(bytes);

            if (count < 36) {
                throw new IllegalStateException("PHONE_BOOT");
            }

            return new String(bytes, 0, count, StandardCharsets.US_ASCII).trim();
        }
    }

    @Override
    public Cursor query(
            Uri uri, String[] projection, String selection, String[] args, String sort) {
        throw new UnsupportedOperationException();
    }

    @Override
    public String getType(Uri uri) {
        throw new UnsupportedOperationException();
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        throw new UnsupportedOperationException();
    }

    @Override
    public int delete(Uri uri, String selection, String[] args) {
        throw new UnsupportedOperationException();
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] args) {
        throw new UnsupportedOperationException();
    }
}
