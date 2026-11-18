package com.voyemu.vehicle;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;
import android.os.Process;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.HashMap;
import java.util.Map;

/** Explicit projection submission and read-only publication receipt, not an OEM UI replacement. */
public final class PassionMcuProvider extends ContentProvider {
    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public Bundle call(String method, String argument, Bundle extras) {
        int caller = Binder.getCallingUid();

        PassionMcuProtocol.authorize(
                caller,
                getContext().checkCallingPermission("android.permission.DUMP")
                        == PackageManager.PERMISSION_GRANTED,
                Process.myUid(),
                getContext().getPackageName());

        if (argument != null
                || (!"submit".equals(method) && !"status".equals(method))
                || ("status".equals(method) && extras != null && !extras.isEmpty())) {
            throw new IllegalArgumentException("PASSION_MCU: closed operation required");
        }

        long identity = Binder.clearCallingIdentity();

        try {
            String boot = PassionMcuBridge.boot();
            PassionMcuProperty bridge = PassionMcuBridge.current();

            if ("submit".equals(method)) {
                if (bridge == null || extras == null || extras.size() != 5) {
                    throw new IllegalStateException(
                            "PASSION_MCU: original transport not ready or invalid extent");
                }

                Map<String, Object> values = new HashMap<String, Object>();

                for (String key : extras.keySet()) {
                    values.put(key, extras.get(key));
                }

                PassionMcuProtocol input = PassionMcuProtocol.decode(values, boot);
                bridge.submit(input.boot, input.generation, input.soc, input.sourceSha256);
            }

            JSONObject receipt = new JSONObject();
            receipt.put("schema", 1);
            receipt.put("boot", boot);
            receipt.put("service_uid", Process.myUid());
            receipt.put("service_pid", Process.myPid());
            receipt.put("full_wire_verified", false);
            receipt.put("visible_effect_verified", false);
            receipt.put("supported_fields", new JSONArray().put("soc"));
            PassionMcuProperty.Snapshot snapshot = bridge == null ? null : bridge.status();
            receipt.put("status", snapshot == null ? "not-ready" : "snapshot_published");

            if (snapshot != null) {
                receipt.put("sequence", snapshot.generation);
                receipt.put("soc", snapshot.soc);
                receipt.put("source_sha256", snapshot.sourceSha256);
                receipt.put("callback_returned", snapshot.callbackReturned);
                receipt.put("property", PassionMcuProperty.SOC);
                receipt.put("area", 0);
                receipt.put("type", "Float");
                receipt.put(
                        "snapshot_readback",
                        bridge.getProperty(PassionMcuProperty.SOC, 0).getValue());
            }

            Bundle result = new Bundle();
            result.putString("json", receipt.toString());

            return result;
        } catch (org.json.JSONException error) {
            throw new IllegalStateException("PASSION_MCU: receipt encoding failed", error);
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
