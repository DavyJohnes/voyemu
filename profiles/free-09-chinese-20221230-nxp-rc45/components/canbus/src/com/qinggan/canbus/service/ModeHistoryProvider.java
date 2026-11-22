package com.qinggan.canbus.service;

/** Read-only mode command history for root or shell with DUMP. */
public final class ModeHistoryProvider extends android.content.ContentProvider {
    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public android.os.Bundle call(String method, String argument, android.os.Bundle extras) {
        int caller = android.os.Binder.getCallingUid();

        if ((caller != 0 && caller != 2000)
                || getContext().checkCallingPermission("android.permission.DUMP")
                        != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            throw new SecurityException("Mode history requires root/shell and DUMP");
        }

        if (!"mode-history".equals(method) || (extras != null && !extras.isEmpty())) {
            throw new IllegalArgumentException("Read-only mode history required");
        }

        long identity = android.os.Binder.clearCallingIdentity();

        try {
            String json = RegistrationService.modeHistory(argument).toString();

            if (json.getBytes(java.nio.charset.StandardCharsets.UTF_8).length > 128 * 1024) {
                throw new IllegalStateException("mode history exceeds response bound");
            }

            android.os.Bundle output = new android.os.Bundle();
            output.putString("json", json);

            return output;
        } catch (org.json.JSONException error) {
            throw new IllegalStateException("Mode history encoding failed", error);
        } finally {
            android.os.Binder.restoreCallingIdentity(identity);
        }
    }

    @Override
    public android.database.Cursor query(
            android.net.Uri uri, String[] projection, String selection, String[] args, String o) {
        throw new UnsupportedOperationException();
    }

    @Override
    public String getType(android.net.Uri uri) {
        throw new UnsupportedOperationException();
    }

    @Override
    public android.net.Uri insert(android.net.Uri uri, android.content.ContentValues values) {
        throw new UnsupportedOperationException();
    }

    @Override
    public int delete(android.net.Uri uri, String selection, String[] args) {
        throw new UnsupportedOperationException();
    }

    @Override
    public int update(
            android.net.Uri uri,
            android.content.ContentValues values,
            String selection,
            String[] args) {
        throw new UnsupportedOperationException();
    }
}
