package com.voyemu.passion.phone;

import android.content.ComponentName;
import android.content.Context;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.telecom.DisconnectCause;
import android.telecom.PhoneAccount;
import android.telecom.PhoneAccountHandle;
import android.telecom.TelecomManager;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.UUID;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;

/** Telecom calls have no radio, SIM, remote endpoint, microphone or audio generator. */
public final class PassionPhoneAdapter {
    public static final Uri URI = Uri.parse("content://com.voyemu.input.phone");
    static final String CALL_ID = "com.voyemu.passion.phone.CALL_ID";
    private static final String ACCOUNT = "voyemu-passion-synthetic",
            INCARNATION = UUID.randomUUID().toString();
    private static final PassionSyntheticPhone PEER = new PassionSyntheticPhone();
    private static volatile PassionConnectionService.LocalConnection connection;

    private PassionPhoneAdapter() {}

    static PassionSyntheticPhone peer() {
        return PEER;
    }

    static PhoneAccountHandle account(Context c) {
        return new PhoneAccountHandle(
                new ComponentName(c, PassionConnectionService.class), ACCOUNT);
    }

    private static TelecomManager telecom(Context c) {
        TelecomManager t = (TelecomManager) c.getSystemService(Context.TELECOM_SERVICE);

        if (t == null) {
            throw new IllegalStateException("PHONE_TELECOM");
        }

        return t;
    }

    public static void initialize(Context c) throws Exception {
        PassionContacts.clear(c);
        telecom(c)
                .registerPhoneAccount(
                        PhoneAccount.builder(account(c), "Voyemu Passion synthetic phone")
                                .setCapabilities(PhoneAccount.CAPABILITY_CALL_PROVIDER)
                                .setSupportedUriSchemes(
                                        Collections.singletonList(PhoneAccount.SCHEME_TEL))
                                .build());
        requireAccount(c);
    }

    private static void requireAccount(Context c) throws Exception {
        PassionPhoneAccount.requireEnabled(telecom(c), account(c));
        PassionPhoneAccount.requireOemDialer(telecom(c));
    }

    static boolean attach(
            PassionConnectionService.LocalConnection value,
            long id,
            String number,
            boolean outgoing) {
        if (connection != null) {
            return false;
        }

        connection = value;

        if (!PEER.deliver(id, number, outgoing)) {
            connection = null;

            return false;
        }

        return true;
    }

    static void detach(PassionConnectionService.LocalConnection value) {
        if (connection == value) {
            connection = null;
        }
    }

    static void serviceDestroyed() {
        PassionConnectionService.LocalConnection call = connection;

        if (call != null) {
            call.finish(DisconnectCause.ERROR);
        }

        PassionSyntheticPhone.Snapshot s = PEER.snapshot();
        PEER.end(s.callId, "service_destroyed");
    }

    private static void onMain(Context c, Runnable action) throws Exception {
        if (Looper.myLooper() == c.getMainLooper()) {
            action.run();

            return;
        }

        FutureTask<Void> task = new FutureTask<Void>(action, null);

        if (!new Handler(c.getMainLooper()).post(task)) {
            throw new IllegalStateException("PHONE_ADAPTER_INIT");
        }

        boolean done = false;

        try {
            task.get(2000, TimeUnit.MILLISECONDS);
            done = true;
        } finally {
            if (!done) {
                task.cancel(false);
            }
        }
    }

    private static void terminate(Context c, long id, String reason) throws Exception {
        // Invalidate the pending ID first: a callback already queued in Telecom cannot revive it.
        PEER.cancelPending(id, reason);
        onMain(
                c,
                () -> {
                    PassionConnectionService.LocalConnection call = connection;

                    if (call != null && call.id == id) {
                        call.finish(DisconnectCause.LOCAL);
                    }
                });
    }

    private static void begin(Context c, String number, boolean outgoing) throws Exception {
        requireAccount(c);
        long id = PEER.begin(number, outgoing);
        boolean delivered = false;

        try {
            Bundle extras = new Bundle();
            extras.putLong(CALL_ID, id);
            Uri address = Uri.fromParts("tel", number, null);

            if (outgoing) {
                Bundle outer = new Bundle();
                outer.putParcelable(TelecomManager.EXTRA_PHONE_ACCOUNT_HANDLE, account(c));
                outer.putBundle(TelecomManager.EXTRA_OUTGOING_CALL_EXTRAS, extras);
                telecom(c).placeCall(address, outer);
            } else {
                extras.putParcelable(TelecomManager.EXTRA_INCOMING_CALL_ADDRESS, address);
                telecom(c).addNewIncomingCall(account(c), extras);
            }

            if (!PEER.awaitDelivery(id, 2000)) {
                throw new IllegalStateException("PHONE_ADAPTER_TIMEOUT");
            }

            delivered = true;
        } finally {
            if (!delivered) {
                terminate(c, id, "delivery_failed");
            }
        }
    }

    public static void outgoing(Context c, String number) throws Exception {
        begin(c, number, true);
    }

    public static JSONObject control(Context c, String method, JSONObject payload)
            throws Exception {
        keys(
                payload,
                "configure".equals(method)
                        ? new String[] {"contacts", "peer_name"}
                        : "incoming".equals(method) ? new String[] {"number"} : new String[] {});

        if ("configure".equals(method)) {
            JSONArray values = payload.getJSONArray("contacts");

            if (values.length() != 2) {
                throw new IllegalArgumentException("PHONE_CONTACTS");
            }

            ArrayList<PassionSyntheticPhone.Contact> contacts =
                    new ArrayList<PassionSyntheticPhone.Contact>();

            for (int i = 0; i < 2; i++) {
                JSONObject value = values.getJSONObject(i);
                keys(value, "name", "number");
                contacts.add(
                        new PassionSyntheticPhone.Contact(
                                value.getString("name"), value.getString("number")));
            }

            PEER.configure(payload.optString("peer_name", "Voyemu Phone"), contacts);
        } else if ("connect".equals(method)) {
            if (PEER.snapshot().state == PassionSyntheticPhone.State.DISCONNECTED) {
                requireAccount(c);
                boolean connected = false;

                try {
                    PassionContacts.replace(c, PEER.snapshot().contacts);
                    PEER.connect();
                    connected = true;
                } finally {
                    if (!connected) {
                        PassionContacts.clear(c);
                    }
                }
            }

            changed(c);
        } else if ("disconnect".equals(method)) {
            PassionSyntheticPhone.Snapshot s = PEER.snapshot();
            terminate(c, s.callId, "disconnect");
            PEER.disconnect();
            PassionContacts.clear(c);
            changed(c);
        } else if ("incoming".equals(method)) {
            begin(c, payload.getString("number"), false);
        } else if ("answer".equals(method)) {
            long id = PEER.snapshot().callId;
            onMain(
                    c,
                    () -> {
                        PassionConnectionService.LocalConnection call = connection;

                        if (call == null || call.id != id || !call.answer()) {
                            throw new IllegalStateException("PHONE_NOT_RINGING_OR_DIALING");
                        }
                    });
        } else if ("hangup".equals(method)) {
            terminate(c, PEER.snapshot().callId, "hangup");
        } else if (!"snapshot".equals(method)) {
            throw new IllegalArgumentException("PHONE_REQUEST");
        }

        return snapshot();
    }

    private static void keys(JSONObject value, String... allowed) throws Exception {
        java.util.List<String> keys = java.util.Arrays.asList(allowed);
        Iterator<String> names = value.keys();

        while (names.hasNext()) {
            if (!keys.contains(names.next())) {
                throw new IllegalArgumentException("PHONE_REQUEST");
            }
        }
    }

    private static void changed(Context c) {
        c.getContentResolver().notifyChange(URI, null);
    }

    public static boolean connected() {
        return PEER.snapshot().state != PassionSyntheticPhone.State.DISCONNECTED;
    }

    static JSONObject snapshot() throws Exception {
        PassionSyntheticPhone.Snapshot s = PEER.snapshot();
        int state =
                s.state == PassionSyntheticPhone.State.INCOMING
                        ? 130
                        : s.state == PassionSyntheticPhone.State.DIALING
                                ? 129
                                : s.state == PassionSyntheticPhone.State.ACTIVE ? 128 : 131;
        JSONArray contacts = new JSONArray();

        for (PassionSyntheticPhone.Contact v : s.contacts) {
            contacts.put(new JSONObject().put("name", v.name).put("number", v.number));
        }

        return new JSONObject()
                .put("ok", true)
                .put("incarnation", INCARNATION)
                .put("sequence", s.revision)
                .put("connected", connected())
                .put("pbap_connected", false)
                .put("sync_state", 0)
                .put("peer_name", s.peerName)
                .put("peer_address", "02:00:00:00:00:01")
                .put(
                        "call",
                        new JSONObject()
                                .put("state", state)
                                .put("id", s.callId)
                                .put("number", s.number)
                                .put("outgoing", s.outgoing)
                                .put("active_started_elapsed_ms", s.activeStarted))
                .put("contacts", contacts)
                .put("outgoing", new JSONArray(s.outgoingCalls))
                .put("events", new JSONArray(s.events))
                .put(
                        "diagnostics",
                        new JSONObject()
                                .put("transport", "synthetic-telecom")
                                .put("pending", s.pending)
                                .put("oem_hfp_delivered", false)
                                .put("oem_pbap_delivered", false));
    }
}
