package com.voyemu.input;

import android.content.Context;
import android.media.AudioManager;
import android.view.KeyEvent;

/** Android key transport; a returned call does not certify OEM handling or sound. */
final class PassionWheelTerminal {
    private PassionWheelTerminal() {}

    static void submit(Context context, PassionWheelSession.Event event) {
        AudioManager audio = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);

        if (audio == null) {
            throw new IllegalStateException("passion-wheel-audio-unavailable");
        }

        if (event.key == 24 || event.key == 25) {
            if (event.action == KeyEvent.ACTION_DOWN) {
                audio.adjustStreamVolume(
                        AudioManager.STREAM_MUSIC,
                        event.key == 24 ? AudioManager.ADJUST_RAISE : AudioManager.ADJUST_LOWER,
                        0);
            }
        } else {
            if (event.key != 85 && event.key != 87 && event.key != 88) {
                throw new IllegalArgumentException("passion-wheel-unknown-key");
            }

            audio.dispatchMediaKeyEvent(
                    KeyEvent.changeFlags(new KeyEvent(event.action, event.key), event.flags));
        }
    }
}
