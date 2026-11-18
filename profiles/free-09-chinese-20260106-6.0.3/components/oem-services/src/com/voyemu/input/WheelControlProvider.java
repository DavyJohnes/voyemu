package com.voyemu.input;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;
import android.util.Log;

import org.json.JSONObject;

/** Narrow root/shell control endpoint: no URI file access and no app-data ADB grants. */
public final class WheelControlProvider extends ContentProvider {
    private static final String TAG = "VoyemuOemStub";
    private static final String CONTROL_PERMISSION = "android.permission.DUMP";

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public Bundle call(String method, String arg, Bundle extras) {
        int callerUid = Binder.getCallingUid();
        boolean permission =
                getContext().checkCallingPermission(CONTROL_PERMISSION)
                        == PackageManager.PERMISSION_GRANTED;

        if (!WheelInputSession.controlCallerAllowed(callerUid, permission)) {
            throw new SecurityException("wheel-control-caller");
        }

        // Subsequent IO/Binder calls run as this app's own UID/SELinux process context.
        long identity = Binder.clearCallingIdentity();

        try {
            WheelControl control = WheelControl.get(getContext());
            JSONObject receipt;

            if ("bootstrap".equals(method)) {
                receipt = control.bootstrap(extras);
            } else if ("input".equals(method)) {
                receipt = control.input(extras);
            } else if ("status".equals(method)) {
                receipt = control.status(arg, extras);
            } else {
                throw new IllegalArgumentException("unknown-wheel-method");
            }

            Bundle result = new Bundle();
            result.putString("json", receipt.toString());

            return result;
        } catch (Exception error) {
            // checkstyle-allow IllegalCatch: every rejected wheel command is a closed-vocabulary
            // reply
            // No Intent/Bundle, config, token, or exception message reaches logs or stdout.
            Log.w(TAG, "wheel control rejected " + error.getClass().getSimpleName());
            Bundle result = new Bundle();
            result.putString(
                    "error",
                    error instanceof SecurityException
                            ? "authentication"
                            : error instanceof IllegalArgumentException
                                    ? "invalid-command"
                                    : "storage-or-delivery");

            return result;
        } finally {
            Binder.restoreCallingIdentity(identity);
        }
    }

    @Override
    public Cursor query(Uri uri, String[] columns, String where, String[] args, String order) {
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
