package com.voyemu.input;

import android.content.Context;
import android.media.AudioManager;

public class StartupAudioTest {
    public static void main(String[] args) throws Exception {
        Context c = new Context();
        AudioManager a = new AudioManager();

        c.service = a;
        java.util.Arrays.fill(a.levels, 5);
        PassionStartupAudioPolicy.zero(c);

        for (int value : a.levels) {
            if (value != 0) {
                throw new AssertionError();
            }
        }

        c.service =
                new AudioManager() {
                    @Override
                    public int getStreamVolume(int stream) {
                        return stream == 6 ? 1 : 0;
                    }
                };
        boolean rejected = false;

        try {
            PassionStartupAudioPolicy.zero(c);
        } catch (IllegalStateException expected) {
            rejected = true;
        }

        if (!rejected) {
            throw new AssertionError("nonzero readback accepted");
        }

        System.out.println("Passion startup audio verifies twelve actual readbacks");
    }
}
