package com.voyemu.input;

import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.AtomicFile;
import android.util.Log;

import com.voyemu.oem.KeyManagerStub;
import com.voyemu.oem.RegistrationService;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.Map;

/** App-owned storage and transient phases; invoked only by the permission-checked provider. */
public final class WheelControl {
    private static final String TAG = "VoyemuOemStub";
    private static WheelControl instance;
    private final Context context;
    private final KeyManagerStub keyManager;
    private WheelInputSession wheelSession;
    private final Handler holdHandler = new Handler(Looper.getMainLooper());
    private final Map<String, Runnable> holdTimers = new LinkedHashMap<>();
    private final Map<String, KeyEventRouter.Delivery> holdDeliveries = new LinkedHashMap<>();

    private WheelControl(Context context) {
        this.context = context.getApplicationContext();
        this.keyManager = RegistrationService.keyManager();
        this.keyManager.attach(this.context);
    }

    static synchronized WheelControl get(Context context) {
        if (instance == null) {
            instance = new WheelControl(context);
        }

        return instance;
    }

    public static synchronized void releaseExisting() {
        if (instance == null) {
            return;
        }

        synchronized (instance) {
            try {
                instance.releaseWheelHolds();
            } catch (Exception error) {
                // checkstyle-allow IllegalCatch: releasing wheel holds on teardown only logs a
                // failure
                Log.w(TAG, "wheel release failed " + error.getClass().getSimpleName());
            }
        }
    }

    synchronized JSONObject bootstrap(Bundle extras) throws Exception {
        if (extras == null
                || WheelInputSession.schemaValue(extras.get("schema"))
                        != WheelInputSession.SCHEMA) {
            throw new IllegalArgumentException("invalid-schema");
        }

        String session = extras.getString("session");
        String token = extras.getString("token");
        // Validate all supplied fields before changing existing state or writing files.
        WheelInputSession.bootstrapMatches(1, session, token, null, null);
        File current = new File(context.getFilesDir(), "wheel-session.json");
        boolean identical = false;

        if (current.exists()) {
            JSONObject previous = readObject(current);

            if (previous.getInt("schema") != 1) {
                throw new IllegalArgumentException("invalid-schema");
            }

            identical =
                    WheelInputSession.bootstrapMatches(
                            1,
                            session,
                            token,
                            previous.getString("session_id"),
                            previous.getString("token"));
        }

        if (!identical) {
            if (wheelSession != null) {
                releaseWheelHolds();
            }

            JSONObject config = new JSONObject();
            config.put("schema", 1);
            config.put("session_id", session);
            config.put("token", token);
            writeObject("wheel-session.json", config);
        }

        loadWheelSession();
        JSONObject receipt = new JSONObject();
        receipt.put("schema", 1);
        receipt.put("session_id", wheelSession.session);
        receipt.put("sequence", wheelSession.sequence());
        receipt.put("status", "session-ready");
        writeObject("wheel-bootstrap-receipt.json", receipt);

        return receipt;
    }

    synchronized JSONObject status(String kind, Bundle extras) throws Exception {
        loadWheelSession();

        if (extras == null
                || !wheelSession.authenticates(
                        WheelInputSession.schemaValue(extras.get("schema")),
                        extras.getString("session"),
                        extras.getString("token"))) {
            throw new SecurityException("wheel-authentication");
        }

        if ("audio".equals(kind)) {
            JSONObject receipt = new JSONObject();
            receipt.put("schema", 1);
            receipt.put("session_id", wheelSession.session);
            receipt.put("sequence", wheelSession.sequence());
            receipt.put("audio_terminal", new JSONObject(keyManager.audioDiagnostics()));

            return receipt;
        }

        String name =
                "bootstrap".equals(kind)
                        ? "wheel-bootstrap-receipt.json"
                        : "input".equals(kind) ? "wheel-receipt.json" : null;

        if (name == null) {
            throw new IllegalArgumentException("invalid-receipt-kind");
        }

        JSONObject receipt = readObject(new File(context.getFilesDir(), name));

        if (!wheelSession.session.equals(receipt.getString("session_id"))) {
            throw new IllegalArgumentException("stale-receipt-session");
        }

        return receipt;
    }

    private static JSONObject readObject(File file) throws Exception {
        if (!file.isFile() || file.length() <= 0 || file.length() > 8192) {
            throw new IllegalArgumentException("invalid-file");
        }

        try (FileInputStream stream = new FileInputStream(file)) {
            byte[] content = new byte[(int) file.length()];
            int offset = 0;

            while (offset < content.length) {
                int count = stream.read(content, offset, content.length - offset);

                if (count < 0) {
                    throw new IllegalArgumentException("truncated-file");
                }

                offset += count;
            }

            if (stream.read() != -1) {
                throw new IllegalArgumentException("changed-file");
            }

            return new JSONObject(new String(content, StandardCharsets.UTF_8));
        }
    }

    private void writeObject(String name, JSONObject object) throws Exception {
        AtomicFile file = new AtomicFile(new File(context.getFilesDir(), name));
        FileOutputStream stream = null;
        boolean written = false;

        try {
            stream = file.startWrite();
            stream.write((object.toString() + "\n").getBytes(StandardCharsets.UTF_8));
            file.finishWrite(stream);
            File target = file.getBaseFile();

            if (!target.setReadable(false, false)
                    || !target.setWritable(false, false)
                    || !target.setReadable(true, true)
                    || !target.setWritable(true, true)) {
                throw new IllegalArgumentException("private-file-mode");
            }

            written = true;
        } finally {
            if (!written && stream != null) {
                file.failWrite(stream);
            }
        }
    }

    private void watermark() throws Exception {
        watermark(null);
    }

    private void watermark(WheelInputSession.Command pending) throws Exception {
        JSONObject object = new JSONObject();

        object.put("schema", WheelInputSession.SCHEMA);
        object.put("session_id", wheelSession.session);
        object.put("sequence", pending == null ? wheelSession.sequence() : pending.sequence);
        Map<String, String> holds = wheelSession.held();

        if (pending != null) {
            if ("down".equals(pending.phase)) {
                holds.put(pending.button, pending.owner);
            } else {
                holds.remove(pending.button);
            }
        }

        object.put("held", new JSONObject(holds));
        writeObject("wheel-watermark.json", object);
    }

    private void loadWheelSession() throws Exception {
        JSONObject config = readObject(new File(context.getFilesDir(), "wheel-session.json"));

        if (config.getInt("schema") != WheelInputSession.SCHEMA) {
            throw new IllegalArgumentException("invalid-schema");
        }

        String session = config.getString("session_id");
        String token = config.getString("token");

        if (wheelSession != null && wheelSession.authenticates(1, session, token)) {
            return;
        }

        long sequence = 0;
        Map<String, String> held = new LinkedHashMap<>();
        File previousFile = new File(context.getFilesDir(), "wheel-watermark.json");

        if (previousFile.exists()) {
            JSONObject previous = readObject(previousFile);

            if (previous.getInt("schema") != 1) {
                throw new IllegalArgumentException("invalid-watermark");
            }

            if (session.equals(previous.getString("session_id"))) {
                sequence = previous.getLong("sequence");
                JSONObject previousHolds = previous.getJSONObject("held");
                Iterator<String> keys = previousHolds.keys();

                while (keys.hasNext()) {
                    String key = keys.next();
                    held.put(key, previousHolds.getString(key));
                }
            }
        }

        if (wheelSession != null) {
            releaseWheelHolds();
        }

        wheelSession = new WheelInputSession(session, token, sequence);
        wheelSession.restoreHeld(held);
        // Restart/session rotation releases outstanding DOWN, never synthesizes SHORT.
        releaseWheelHolds();
    }

    private void releaseWheelHolds() throws Exception {
        if (wheelSession == null) {
            return;
        }

        for (Runnable timer : holdTimers.values()) {
            holdHandler.removeCallbacks(timer);
        }

        holdTimers.clear();
        holdDeliveries.clear();

        for (String button : wheelSession.held().keySet()) {
            keyManager.dispatchQG(WheelInputSession.keyFor(button), WheelInputSession.UP);
        }

        wheelSession.clearHeld();
        watermark();
    }

    private void scheduleHold(final String button, final String owner) {
        if (!WheelInputSession.supportsLong(button)) {
            return;
        }

        final WheelInputSession session = wheelSession;
        Runnable timer =
                new Runnable() {
                    @Override
                    public void run() {
                        synchronized (WheelControl.this) {
                            if (wheelSession != session || holdTimers.get(button) != this) {
                                return;
                            }

                            holdTimers.remove(button);

                            if (session.startLong(button, owner)) {
                                holdDeliveries.put(
                                        button,
                                        keyManager.dispatchQG(
                                                WheelInputSession.keyFor(button),
                                                WheelInputSession.LONG));
                            }
                        }
                    }
                };
        holdTimers.put(button, timer);
        holdHandler.postDelayed(timer, WheelInputSession.LONG_PRESS_MS);
    }

    /** Ends the hold timer of button and adds its held delivery; the caller holds this lock. */
    private KeyEventRouter.Delivery releaseHold(String button, KeyEventRouter.Delivery delivery) {
        Runnable timer = holdTimers.remove(button);

        if (timer != null) {
            holdHandler.removeCallbacks(timer);
        }

        KeyEventRouter.Delivery heldDelivery = holdDeliveries.remove(button);

        if (heldDelivery != null) {
            delivery.add(heldDelivery);
        }

        return heldDelivery;
    }

    synchronized JSONObject input(Bundle extras) throws Exception {
        if (extras == null) {
            throw new IllegalArgumentException("missing-extras");
        }

        loadWheelSession();

        if (!wheelSession.authenticates(
                WheelInputSession.schemaValue(extras.get("schema")),
                extras.getString("session"),
                extras.getString("token"))) {
            throw new SecurityException("wheel-authentication");
        }

        long sequence = extras.getLong("sequence", -1);
        String button = extras.getString("button");
        String phase = extras.getString("phase");
        String owner = extras.getString("owner");
        WheelInputSession.Command command = wheelSession.prepare(sequence, button, phase, owner);
        // Persist before committing memory or callbacks; failed IO cannot create a phantom hold.
        watermark(command);
        wheelSession.commit(command);
        KeyEventRouter.Delivery delivery = new KeyEventRouter.Delivery();
        KeyEventRouter.Delivery heldDelivery =
                "down".equals(phase) ? null : releaseHold(button, delivery);
        JSONArray actions = new JSONArray();

        for (int action : command.actions) {
            delivery.add(keyManager.dispatchQG(command.key, action));
            actions.put(action);
        }

        JSONObject receipt = new JSONObject();
        receipt.put("schema", 1);
        receipt.put("session_id", wheelSession.session);
        receipt.put("sequence", sequence);
        receipt.put("button", button);
        receipt.put("phase", phase);
        receipt.put("key_code", command.key);
        receipt.put(
                "translated_key_code",
                keyManager.getMappingKeyEvent(command.key, "com.voyemu.oem"));
        receipt.put("actions", actions);

        if (heldDelivery != null) {
            receipt.put("hold_actions", new JSONArray().put(WheelInputSession.LONG));
        }

        receipt.put("callbacks", delivery.callbacks);
        receipt.put("callback_receipts", new JSONArray(delivery.callbackReceipts));
        receipt.put(
                "callback_receipts_truncated",
                delivery.callbacks > delivery.callbackReceipts.size());
        receipt.put("terminals", delivery.terminals);
        receipt.put("consumed", delivery.consumed);
        receipt.put("src_callback_notified", delivery.srcNotified);

        if (delivery.terminalFailure != null) {
            receipt.put("terminal_failure", delivery.terminalFailure);
        }

        if (delivery.terminalDiagnostics != null) {
            receipt.put("audio_terminal", new JSONObject(delivery.terminalDiagnostics));
        }

        if (delivery.volume != null) {
            JSONObject volume = new JSONObject();
            volume.put("stream", delivery.volume.stream);
            volume.put("before", delivery.volume.before);
            volume.put("after", delivery.volume.after);
            volume.put("flags", AudioControl.WHEEL_FLAGS);
            volume.put("changed", delivery.volume.before != delivery.volume.after);
            receipt.put("volume_change", volume);
        }

        receipt.put("status", KeyManagerStub.deliveryStatus(delivery));
        writeObject("wheel-receipt.json", receipt);

        if ("down".equals(phase)) {
            scheduleHold(button, owner);
        }

        Log.i(
                TAG,
                "wheel sequence="
                        + sequence
                        + " button="
                        + button
                        + " phase="
                        + phase
                        + " callbacks="
                        + delivery.callbacks
                        + " terminals="
                        + delivery.terminals);

        return receipt;
    }
}
