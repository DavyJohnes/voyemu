package com.voyemu.window;

import android.content.Context;
import android.media.AudioManager;
import android.widget.SeekBar;

/** Optional owner Bluetooth stream12, never an alias for the SDK music stream. */
public final class PassionBluetoothVolume {
    private PassionBluetoothVolume() {}

    public static boolean available(AudioManager manager) {
        if (manager == null) {
            return false;
        }

        try {
            manager.getStreamMaxVolume(12);

            return true;
        } catch (IllegalArgumentException error) {
            if ("Bad stream type 12".equals(error.getMessage())) {
                return false;
            }

            throw error;
        }
    }

    public static int getStreamVolume(AudioManager manager, int stream) {
        return stream == 12 && !available(manager) ? -1 : manager.getStreamVolume(stream);
    }

    public static int getStreamMaxVolume(AudioManager manager, int stream) {
        return stream == 12 && !available(manager) ? -1 : manager.getStreamMaxVolume(stream);
    }

    public static int getStreamMinVolume(AudioManager manager, int stream) {
        return stream == 12 && !available(manager) ? -1 : manager.getStreamMinVolume(stream);
    }

    public static void setStreamVolume(AudioManager manager, int stream, int volume, int flags) {
        if (stream != 12 || available(manager)) {
            manager.setStreamVolume(stream, volume, flags);
        }
    }

    public static void setSecondaryMax(SeekBar view, int maximum) {
        if (maximum < 0) {
            view.setEnabled(false);

            return;
        }

        view.setMax(maximum);
    }

    public static void refreshSecondary(SeekBar view, boolean earConnected) {
        if (view == null) {
            return;
        }

        AudioManager manager =
                (AudioManager) view.getContext().getSystemService(Context.AUDIO_SERVICE);
        view.setEnabled(!earConnected || available(manager));
    }
}
