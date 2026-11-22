package com.qinggan.canbus.service;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;

import org.json.JSONObject;

/** Read-only publication receipt; normal OEM clients continue using their original Binder API. */
public final class VehicleStateProvider extends ContentProvider {
    @Override
    public boolean onCreate() {
        return true;
    }

    /** Root or shell with DUMP, a known method, an argument only for a query and no extras. */
    private void validate(boolean known, boolean query, String argument, Bundle extras) {
        int caller = Binder.getCallingUid();

        if ((caller != 0 && caller != 2000)
                || getContext().checkCallingPermission("android.permission.DUMP")
                        != PackageManager.PERMISSION_GRANTED) {
            throw new SecurityException("Vehicle state receipt requires root/shell and DUMP");
        }

        if (!known || (!query && argument != null) || (extras != null && !extras.isEmpty())) {
            throw new IllegalArgumentException("Read-only vehicle status required");
        }
    }

    @Override
    public Bundle call(String method, String argument, Bundle extras) {
        boolean query =
                "observe".equals(method)
                        || "ipk-history".equals(method)
                        || "mode-history".equals(method);

        validate(
                query || "status".equals(method) || "phone-diagnostics".equals(method),
                query,
                argument,
                extras);

        long identity = Binder.clearCallingIdentity();

        try {
            if ("phone-diagnostics".equals(method)) {
                Bundle output = new Bundle();
                output.putString("json", RegistrationService.phoneDiagnostics().toString());

                return output;
            }

            if (query) {
                Bundle output = new Bundle();
                String json =
                        ("observe".equals(method)
                                        ? RegistrationService.currentObservation(argument)
                                        : "ipk-history".equals(method)
                                                ? RegistrationService.ipkHistory(argument)
                                                : RegistrationService.modeHistory(argument))
                                .toString();

                if (json.getBytes(java.nio.charset.StandardCharsets.UTF_8).length > 128 * 1024) {
                    throw new IllegalStateException("observation exceeds response bound");
                }

                output.putString("json", json);

                return output;
            }

            JSONObject receipt = new JSONObject();
            receipt.put("schema", 1);
            VehicleInputSnapshot snapshot = RegistrationService.currentInputs();

            if (snapshot == null) {
                receipt.put("status", "not-ready");
            } else {
                JSONObject raw = new JSONObject();

                for (String name : VehicleSignalCatalog.NAMES) {
                    raw.put(name, snapshot.value(name));
                }

                receipt.put("status", "applied");
                receipt.put("sequence", snapshot.sequence);
                receipt.put("map_sha256", VehicleSignalCatalog.SHA256);
                receipt.put("field_count", VehicleSignalCatalog.NAMES.length);
                receipt.put("raw_oem", raw);
                receipt.put(
                        "typed_projection",
                        new JSONObject().put("TJA_HWA_STS", snapshot.tjaState()));
            }

            Bundle output = new Bundle();
            output.putString("json", receipt.toString());

            return output;
        } catch (org.json.JSONException error) {
            throw new IllegalStateException("Vehicle receipt encoding failed", error);
        } finally {
            Binder.restoreCallingIdentity(identity);
        }
    }

    @Override
    public Cursor query(
            Uri uri, String[] projection, String selection, String[] args, String order) {
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
