package com.qinggan.canbus.service;

import android.content.SharedPreferences;
import android.os.SystemClock;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.Iterator;
import java.util.Map;

/**
 * Mode commands the vehicle model of the component accepted, in acceptance order: DRIVING_MODE_SET
 * with the packet it came in and the separate IVI_SOC_MODESET. The history belongs to one service
 * instance; it is complete when it holds every command of the Android boot.
 */
public final class ModeCommandHistory {
    public static final String DRIVING_MODE_SET = "DRIVING_MODE_SET";
    public static final String IVI_SOC_MODESET = "IVI_SOC_MODESET";

    /** The command came from the BCM PEPS ACC ON handler of the component. */
    public static final String ACC_ON = "acc_on";

    /** The command came from a client of the component. */
    public static final String CLIENT = "client";

    private final CanObservationJournal mJournal = new CanObservationJournal(256);
    private final String mBootId;
    private final String mSourceInstance;
    private final String mIncomplete;

    /**
     * Starts the history of this service instance; the preferences remember the boot of the
     * previous instance, so a restart within the same boot marks the history incomplete.
     */
    public ModeCommandHistory(SharedPreferences preferences, String bootId, String sourceInstance) {
        mBootId = bootId;
        mSourceInstance = sourceInstance;

        if (bootId == null) {
            mIncomplete = "boot_unknown";
        } else if (bootId.equals(preferences.getString("boot_id", null))) {
            mIncomplete = "service_restarted";
        } else {
            mIncomplete =
                    preferences.edit().putString("boot_id", bootId).commit()
                            ? null
                            : "boot_record_failed";
        }
    }

    /** Records an accepted command; the packet holds the command and the states sent with it. */
    public synchronized void accepted(String command, Map<String, Integer> packet, String origin) {
        try {
            JSONObject states = new JSONObject();

            for (Map.Entry<String, Integer> state : packet.entrySet()) {
                states.put(state.getKey(), (int) state.getValue());
            }

            mJournal.append(
                    new JSONObject()
                            .put("seq", Long.toString(mJournal.next()))
                            .put("command", command)
                            .put("value", (int) packet.get(command))
                            .put("packet", states)
                            .put("origin", origin)
                            .put("elapsed_ns", Long.toString(SystemClock.elapsedRealtimeNanos()))
                            .toString());
        } catch (JSONException failure) {
            throw new IllegalStateException(failure);
        }
    }

    private static int limit(JSONObject query) throws JSONException {
        Iterator<String> keys = query.keys();

        while (keys.hasNext()) {
            String key = keys.next();

            if (!"schema".equals(key) && !"cursor".equals(key) && !"limit".equals(key)) {
                throw new IllegalArgumentException("unknown mode history argument");
            }
        }

        if (!query.has("schema") || !Integer.valueOf(1).equals(query.get("schema"))) {
            throw new IllegalArgumentException("mode history schema 1 required");
        }

        Object value = query.has("limit") ? query.get("limit") : 128;

        if (!(value instanceof Integer) || (Integer) value < 1 || (Integer) value > 256) {
            throw new IllegalArgumentException("mode history limit 1..256");
        }

        return (Integer) value;
    }

    private static JSONObject cursor(JSONObject query) throws JSONException {
        JSONObject cursor = query.getJSONObject("cursor");

        if (cursor.length() != 2
                || !(cursor.get("source_instance") instanceof String)
                || !(cursor.get("after_seq") instanceof String)
                || !cursor.getString("after_seq").matches("0|[1-9][0-9]{0,18}")) {
            throw new IllegalArgumentException("invalid mode history cursor");
        }

        return cursor;
    }

    private static JSONObject query(String request) throws JSONException {
        if (request == null || request.length() > 1024) {
            throw new IllegalArgumentException("bounded mode history query required");
        }

        return new JSONObject(request);
    }

    /**
     * Answers a request {"schema":1} with an optional cursor {"source_instance","after_seq"} and
     * limit 1..256.
     */
    public synchronized JSONObject history(String request) throws JSONException {
        JSONObject query = query(request);
        int limit = limit(query);
        JSONObject cursor = query.has("cursor") ? cursor(query) : null;
        long oldest = mJournal.oldest();
        long latest = mJournal.latest();
        long after = 0;
        String gap = null;

        if (cursor != null) {
            after = Long.parseLong(cursor.getString("after_seq"));

            if (!mSourceInstance.equals(cursor.getString("source_instance"))) {
                gap = "source_replaced";
                after = 0;
            } else if (after > latest) {
                gap = "cursor_ahead";
                after = 0;
            } else if (after < oldest - 1) {
                gap = "overflow";
            }
        }

        JSONArray events = new JSONArray();

        for (CanObservationJournal.Record record : mJournal.after(after, limit)) {
            events.put(new JSONObject(record.json));
            after = record.seq;
        }

        String incomplete = mIncomplete != null ? mIncomplete : oldest > 1 ? "overflow" : null;

        return new JSONObject()
                .put("schema", 1)
                .put("source", "android:qg.canbus:mode")
                .put("status", "observed")
                .put("boot_id", mBootId == null ? JSONObject.NULL : mBootId)
                .put("source_instance", mSourceInstance)
                .put("complete", incomplete == null)
                .put("incomplete_reason", incomplete == null ? JSONObject.NULL : incomplete)
                .put("oldest_seq", Long.toString(oldest))
                .put("latest_seq", Long.toString(latest))
                .put("gap", gap == null ? JSONObject.NULL : gap)
                .put("events", events)
                .put(
                        "next_cursor",
                        new JSONObject()
                                .put("source_instance", mSourceInstance)
                                .put("after_seq", Long.toString(after)));
    }
}
