package com.qinggan.canbus.service;

import com.qinggan.canbus.VehicleState;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.Iterator;
import java.util.LinkedHashSet;

final class CanObservationRequest {
    final LinkedHashSet<VehicleState> states = new LinkedHashSet<>();
    String instance;
    long after;
    int limit = 32;

    static CanObservationRequest parse(String text) throws JSONException {
        if (text == null || text.getBytes(java.nio.charset.StandardCharsets.UTF_8).length > 4096) {
            throw new IllegalArgumentException("observe request size");
        }

        JSONObject json = new JSONObject(text);
        requireKnownFields(json);

        if (!(json.get("schema") instanceof Integer) || json.getInt("schema") != 1) {
            throw new IllegalArgumentException("observe schema must be 1");
        }

        CanObservationRequest result = new CanObservationRequest();

        if (json.has("limit")) {
            if (!(json.get("limit") instanceof Integer)) {
                throw new IllegalArgumentException("integer limit required");
            }

            result.limit = json.getInt("limit");
        }

        if (result.limit < 1 || result.limit > 32) {
            throw new IllegalArgumentException("observe limit");
        }

        if (json.has("states")) {
            parseStates(json.getJSONArray("states"), result);
        }

        if (result.states.isEmpty()) {
            result.states.add(VehicleState.IVI_SOC_MODESET);
            result.states.add(VehicleState.HUM_VSP_FUNCTION_SW);
        }

        if (json.has("cursor")) {
            parseCursor(json.getJSONObject("cursor"), result);
        }

        return result;
    }

    private static void requireKnownFields(JSONObject json) {
        for (Iterator<String> keys = json.keys(); keys.hasNext(); ) {
            String key = keys.next();

            if (!key.equals("schema")
                    && !key.equals("states")
                    && !key.equals("cursor")
                    && !key.equals("limit")) {
                throw new IllegalArgumentException("unknown observe field: " + key);
            }
        }
    }

    private static void parseStates(JSONArray states, CanObservationRequest result)
            throws JSONException {
        if (states.length() > 32) {
            throw new IllegalArgumentException("too many states");
        }

        for (int i = 0; i < states.length(); i++) {
            if (!(states.get(i) instanceof String)) {
                throw new IllegalArgumentException("state name required");
            }

            if (!result.states.add(VehicleState.valueOf(states.getString(i)))) {
                throw new IllegalArgumentException("duplicate state");
            }
        }
    }

    private static void parseCursor(JSONObject cursor, CanObservationRequest result)
            throws JSONException {
        if (cursor.length() != 2
                || !(cursor.get("source_instance") instanceof String)
                || !(cursor.get("after_seq") instanceof String)) {
            throw new IllegalArgumentException("invalid cursor");
        }

        result.instance = cursor.getString("source_instance");
        String after = cursor.getString("after_seq");

        if (result.instance.length() < 1
                || result.instance.length() > 128
                || !after.matches("0|[1-9][0-9]{0,18}")) {
            throw new IllegalArgumentException("invalid cursor");
        }

        result.after = Long.parseLong(after);
    }
}
