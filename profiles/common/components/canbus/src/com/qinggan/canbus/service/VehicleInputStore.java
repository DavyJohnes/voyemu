package com.qinggan.canbus.service;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;

import org.json.JSONException;
import org.json.JSONObject;

import java.nio.charset.StandardCharsets;
import java.time.Instant;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;

/** Bounded complete input and durable app-private cache; OEM configuration is separate. */
final class VehicleInputStore {
    private static final int MAX_BYTES = 128 * 1024;
    private final SharedPreferences preferences;

    VehicleInputStore(Context context) {
        preferences = context.getSharedPreferences("voyemu-vehicle-input", Context.MODE_PRIVATE);
    }

    VehicleInputSnapshot load() {
        String value = preferences.getString("snapshot", null);

        if (value == null) {
            return null;
        }

        try {
            return parse(value);
        } catch (JSONException | IllegalArgumentException | java.time.DateTimeException e) {
            Log.e("CanBusStub", "Invalid saved vehicle input; preserved for diagnosis", e);

            return null;
        }
    }

    boolean save(VehicleInputSnapshot snapshot) {
        try {
            JSONObject raw = new JSONObject();

            for (String name : VehicleSignalCatalog.NAMES) {
                raw.put(name, snapshot.value(name));
            }

            JSONObject state = new JSONObject();
            state.put("schema", 1)
                    .put("sequence", snapshot.sequence)
                    .put("updated_utc", Instant.now().toString())
                    .put("map_sha256", VehicleSignalCatalog.SHA256)
                    .put("origin", "manual-emulator")
                    .put("raw_oem", raw);

            return preferences.edit().putString("snapshot", state.toString()).commit();
        } catch (JSONException e) {
            Log.e("CanBusStub", "Cannot persist complete vehicle input", e);

            return false;
        }
    }

    static VehicleInputSnapshot parse(String text) throws JSONException {
        if (text == null || text.getBytes(StandardCharsets.UTF_8).length > MAX_BYTES) {
            throw new IllegalArgumentException("Vehicle input extent");
        }

        JSONObject state = new JSONObject(text);

        if (state.length() != 6
                || exactInteger(state.get("schema")) != 1
                || !VehicleSignalCatalog.SHA256.equals(state.get("map_sha256"))
                || !"manual-emulator".equals(state.get("origin"))) {
            throw new IllegalArgumentException("Pinned vehicle input schema");
        }

        Object timestamp = state.get("updated_utc");

        if (!(timestamp instanceof String)
                || ((String) timestamp).length() > 40
                || !OffsetDateTime.parse((String) timestamp).getOffset().equals(ZoneOffset.UTC)) {
            throw new IllegalArgumentException("Vehicle input UTC timestamp");
        }

        long sequence = exactInteger(state.get("sequence"));
        JSONObject raw = state.getJSONObject("raw_oem");

        if (raw.length() != VehicleSignalCatalog.NAMES.length) {
            throw new IllegalArgumentException("All OEM signals required");
        }

        long[] values = new long[VehicleSignalCatalog.NAMES.length];

        for (int i = 0; i < values.length; i++) {
            long value = exactInteger(raw.get(VehicleSignalCatalog.NAMES[i]));

            if (value < 0 || value > VehicleSignalCatalog.MAXIMA[i]) {
                throw new IllegalArgumentException("OEM signal bit width");
            }

            values[i] = value;
        }

        return new VehicleInputSnapshot(sequence, values);
    }

    private static long exactInteger(Object value) {
        if (!(value instanceof Integer) && !(value instanceof Long)) {
            throw new IllegalArgumentException("Integer OEM value required");
        }

        return ((Number) value).longValue();
    }
}
