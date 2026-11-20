package com.qinggan.audiopolicy;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.media.AudioManager;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.Arrays;
import java.util.HashSet;

/** Host-only playback reset and synchronous pre-snapshot acknowledgement. */
public class AudioStartupProvider extends ContentProvider {
    @Override
    public boolean onCreate() {
        return true;
    }

    protected String currentBoot() {
        return StartupAudioPolicy.boot();
    }

    @Override
    public Bundle call(String operation, String argument, Bundle extras) {
        validateRequest(argument, extras);
        String session = extras.getString("launch_session"), boot = extras.getString("boot_id");
        long generation = (Long) extras.get("android_generation");
        StartupAudioPolicy.key(boot, session);

        if (generation <= 0 || !boot.equals(currentBoot())) {
            throw new IllegalArgumentException("STARTUP_AUDIO_IDENTITY");
        }

        if (!"reset_launch".equals(operation)
                && !"prepare_snapshot".equals(operation)
                && !"status".equals(operation)) {
            throw new IllegalArgumentException("STARTUP_AUDIO_OPERATION");
        }

        long identity = Binder.clearCallingIdentity();

        try {
            synchronized (StartupAudioPolicy.class) {
                Context context = getContext();

                if ("reset_launch".equals(operation)) {
                    StartupAudioPolicy.launch(context, boot, session, generation, false);
                } else {
                    if (!StartupAudioPolicy.completed(context, boot, session, generation)) {
                        throw new IllegalStateException("STARTUP_AUDIO_OWNER");
                    }

                    if ("prepare_snapshot".equals(operation)) {
                        StartupAudioPolicy.launch(context, boot, session, generation, true);
                    }
                }

                try {
                    return receipt(context, boot, session, generation);
                } catch (org.json.JSONException failure) {
                    throw new IllegalStateException("STARTUP_AUDIO_REPLY", failure);
                }
            }
        } finally {
            Binder.restoreCallingIdentity(identity);
        }
    }

    private void validateRequest(String argument, Bundle extras) {
        int uid = Binder.getCallingUid();

        if ((uid != 0 && uid != 2000)
                || getContext().checkCallingPermission("android.permission.DUMP")
                        != PackageManager.PERMISSION_GRANTED) {
            throw new SecurityException("STARTUP_AUDIO_PERMISSION");
        }

        if (argument != null
                || extras == null
                || !extras.keySet()
                        .equals(
                                new HashSet<String>(
                                        Arrays.asList(
                                                "launch_session", "boot_id", "android_generation")))
                || !(extras.get("launch_session") instanceof String)
                || !(extras.get("boot_id") instanceof String)
                || !(extras.get("android_generation") instanceof Long)) {
            throw new IllegalArgumentException("STARTUP_AUDIO_REQUEST");
        }
    }

    private static Bundle receipt(Context context, String boot, String session, long generation)
            throws org.json.JSONException {
        AudioManager audio = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);

        if (audio == null) {
            throw new IllegalStateException("STARTUP_AUDIO_UNAVAILABLE");
        }

        JSONArray streams = new JSONArray();
        boolean zero = true;

        for (int stream = 0; stream < 11; stream++) {
            int value = audio.getStreamVolume(stream);
            streams.put(value);
            zero &= value == 0;
        }

        SharedPreferences volumes =
                context.getSharedPreferences("oem_audio_volume", Context.MODE_PRIVATE);
        boolean oemZero = true;

        for (int stream = 0; stream <= 24; stream++) {
            oemZero &= volumes.getInt("stream-" + stream, -1) == 0;
        }

        JSONObject receipt =
                new JSONObject()
                        .put("schema", 1)
                        .put("launch_session", session)
                        .put("boot_id", boot)
                        .put("android_generation", generation)
                        .put(
                                "completed",
                                StartupAudioPolicy.completed(context, boot, session, generation))
                        .put("all_zero", zero && oemZero)
                        .put("stream_readbacks", streams)
                        .put("oem25_zero", oemZero);
        Bundle result = new Bundle();
        result.putString("json", receipt.toString());

        return result;
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
