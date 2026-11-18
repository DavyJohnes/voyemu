package com.voyemu.settings;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioManager;

import androidx.preference.Preference;
import androidx.preference.PreferenceGroup;

import com.android.car.settings.common.SeekBarPreference;

import java.util.IdentityHashMap;

/** Standard Android streams only, never advertised as OEM car audio groups. */
public final class PassionSoundStreams {
    private static final int[] STREAMS = {
        AudioManager.STREAM_MUSIC,
        AudioManager.STREAM_VOICE_CALL,
        AudioManager.STREAM_ALARM,
        AudioManager.STREAM_RING,
        AudioManager.STREAM_NOTIFICATION
    };
    private static final String[] LABELS_EN = {
        "Media", "Call", "Alarm", "Ringtone", "Notifications"
    };
    private static final String[] LABELS_RU = {
        "Медиа", "Звонок", "Будильник", "Мелодия звонка", "Уведомления"
    };
    private static final IdentityHashMap<PreferenceGroup, BroadcastReceiver> RECEIVERS =
            new IdentityHashMap<>();

    private PassionSoundStreams() {}

    public static void update(PreferenceGroup group) {
        Context context = group.getContext();

        if (!RECEIVERS.containsKey(group)) {
            BroadcastReceiver receiver =
                    new BroadcastReceiver() {
                        @Override
                        public void onReceive(Context ignored, Intent intent) {
                            refresh(group);
                        }
                    };
            IntentFilter filter = new IntentFilter("android.media.VOLUME_CHANGED_ACTION");
            filter.addAction("android.media.STREAM_MUTE_CHANGED_ACTION");
            context.registerReceiver(receiver, filter);
            RECEIVERS.put(group, receiver);
        }

        refresh(group);
    }

    public static void dispose(PreferenceGroup group) {
        if (group == null) {
            return;
        }

        BroadcastReceiver receiver = RECEIVERS.remove(group);

        if (receiver != null) {
            group.getContext().unregisterReceiver(receiver);
        }
    }

    private static void refresh(PreferenceGroup group) {
        Context context = group.getContext();
        AudioManager audio = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);

        if (audio == null) {
            throw new IllegalStateException("passion-sound-audio-unavailable");
        }

        String language =
                context.getResources().getConfiguration().getLocales().get(0).getLanguage();
        String[] labels = "ru".equals(language) ? LABELS_RU : LABELS_EN;

        for (int i = 0; i < STREAMS.length; i++) {
            final int stream = STREAMS[i];
            String key = "voyemu_android_stream_" + stream;
            Preference existing = group.findPreference(key);
            SeekBarPreference item;

            if (existing == null) {
                item = new SeekBarPreference(context);
                item.setKey(key);
                item.setPersistent(false);
                item.setTitle(labels[i]);
                item.setContinuousUpdate(true);
                item.setShowSeekBarValue(false);
                item.setOnPreferenceChangeListener((preference, value) -> {
                    if (!(value instanceof Integer)) {
                        return false;
                    }

                    int requested = (Integer) value,
                            min = audio.getStreamMinVolume(stream),
                            max = audio.getStreamMaxVolume(stream);

                    if (requested < min || requested > max) {
                        return false;
                    }

                    audio.setStreamVolume(stream, requested, 0);
                    boolean applied = audio.getStreamVolume(stream) == requested;

                    if (applied) {
                        refresh(group);
                    }

                    return applied;
                });
                group.addPreference(item);
            } else if (existing instanceof SeekBarPreference) {
                item = (SeekBarPreference) existing;
            } else {
                throw new IllegalStateException("passion-sound-preference-collision");
            }

            item.setTitle(labels[i]);
            item.setMin(audio.getStreamMinVolume(stream));
            item.setMax(audio.getStreamMaxVolume(stream));
            item.setValue(audio.getStreamVolume(stream));
        }
    }
}
