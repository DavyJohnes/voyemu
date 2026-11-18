package com.voyemu.input;

import android.content.Context;
import android.media.AudioManager;
import android.util.Log;

/** Called only by exact Free OEM wheel sites; never during startup to suppress sound. */
public final class FreeWheelMediaActions {
    private static final class AndroidMixer implements FreeWheelVolume.Mixer {
        final AudioManager audio;

        AndroidMixer(Context context) {
            if (context == null) {
                throw new IllegalArgumentException("media-context");
            }

            audio = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);

            if (audio == null) {
                throw new IllegalStateException("media-audio-unavailable");
            }
        }

        @Override
        public int mode() {
            return audio.getMode();
        }

        @Override
        public boolean bluetoothSco() {
            return audio.isBluetoothScoOn();
        }

        @Override
        public int minimum() {
            return audio.getStreamMinVolume(AudioManager.STREAM_MUSIC);
        }

        @Override
        public int maximum() {
            return audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC);
        }

        @Override
        public int volume() {
            return audio.getStreamVolume(AudioManager.STREAM_MUSIC);
        }

        @Override
        public boolean muted() {
            return audio.isStreamMute(AudioManager.STREAM_MUSIC);
        }

        @Override
        public void adjust(int direction) {
            audio.adjustStreamVolume(AudioManager.STREAM_MUSIC, direction, 0);
        }

        @Override
        public void mute(boolean value) {
            audio.adjustStreamVolume(
                    AudioManager.STREAM_MUSIC,
                    value ? AudioManager.ADJUST_MUTE : AudioManager.ADJUST_UNMUTE,
                    0);
        }
    }

    private static void observe(FreeWheelVolume.Result value) {
        // Finite numeric diagnostics only. This is a mixer observation, not an OEM callback or UI
        // ACK.
        Log.i(
                "VoyemuWheel",
                "media status="
                        + value.status
                        + " stream=3 before="
                        + value.before
                        + " after="
                        + value.after
                        + " muted_before="
                        + value.mutedBefore
                        + " muted_after="
                        + value.mutedAfter);
    }

    public static void adjustVolume(Context context, int direction, int requestFlags) {
        // OEM requested UI/sound flags do not authorize a stock overlay or an extra tone.
        observe(FreeWheelVolume.adjust(new AndroidMixer(context), direction));
    }

    public static boolean isMediaMuted(Context context) {
        return new AndroidMixer(context).muted();
    }

    public static void setMediaMuted(Context context, boolean muted, int requestFlags) {
        observe(FreeWheelVolume.mute(new AndroidMixer(context), muted));
    }

    private FreeWheelMediaActions() {}
}
