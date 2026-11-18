package com.voyemu.prepare;

import android.content.Context;
import android.media.AudioManager;
import android.widget.SeekBar;

import com.voyemu.window.PassionBluetoothVolume;

public final class PassionBluetoothVolumeTest {
    static void check(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    interface Action {
        void run();
    }

    static void reject(Action a) {
        try {
            a.run();
        } catch (IllegalArgumentException expected) {
            return;
        }

        throw new AssertionError();
    }

    public static void main(String[] args) {
        AudioManager sdk = new AudioManager();

        for (int stream = 0; stream < 12; stream++) {
            check(PassionBluetoothVolume.getStreamMaxVolume(sdk, stream) == 21);
            check(PassionBluetoothVolume.getStreamMinVolume(sdk, stream) == 2);
            check(PassionBluetoothVolume.getStreamVolume(sdk, stream) == 7);
            PassionBluetoothVolume.setStreamVolume(sdk, stream, 6, 0);
            check(sdk.lastStream == stream);
        }

        int writes = sdk.writes;
        check(!PassionBluetoothVolume.available(sdk));
        check(
                PassionBluetoothVolume.getStreamMaxVolume(sdk, 12) == -1
                        && PassionBluetoothVolume.getStreamMinVolume(sdk, 12) == -1
                        && PassionBluetoothVolume.getStreamVolume(sdk, 12) == -1);
        PassionBluetoothVolume.setStreamVolume(sdk, 12, 10, 0);
        check(sdk.writes == writes);
        reject(() -> PassionBluetoothVolume.getStreamMaxVolume(sdk, 13));
        reject(() -> PassionBluetoothVolume.getStreamVolume(sdk, -1));
        reject(() -> PassionBluetoothVolume.setStreamVolume(sdk, 13, 10, 0));
        sdk.unrelated = true;
        reject(() -> PassionBluetoothVolume.getStreamMaxVolume(sdk, 12));
        sdk.unrelated = false;
        AudioManager owner = new AudioManager();
        owner.count = 13;
        check(
                PassionBluetoothVolume.available(owner)
                        && PassionBluetoothVolume.getStreamMaxVolume(owner, 12) == 21
                        && PassionBluetoothVolume.getStreamMinVolume(owner, 12) == 2
                        && PassionBluetoothVolume.getStreamVolume(owner, 12) == 7);
        PassionBluetoothVolume.setStreamVolume(owner, 12, 11, 0);
        check(owner.writes == 1 && owner.lastStream == 12 && owner.lastVolume == 11);
        SeekBar bar = new SeekBar(new Context(sdk));
        PassionBluetoothVolume.setSecondaryMax(bar, -1);
        check(!bar.enabled && bar.max == 9 && bar.maxWrites == 0);
        PassionBluetoothVolume.refreshSecondary(bar, true);
        check(!bar.enabled);
        bar.setMax(21);
        PassionBluetoothVolume.refreshSecondary(bar, true);
        check(!bar.enabled);
        PassionBluetoothVolume.refreshSecondary(bar, false);
        check(bar.enabled && bar.max == 21);
        PassionBluetoothVolume.refreshSecondary(bar, true);
        check(!bar.enabled);
        PassionBluetoothVolume.setSecondaryMax(bar, 21);
        PassionBluetoothVolume.refreshSecondary(bar, true);
        check(!bar.enabled);
        PassionBluetoothVolume.refreshSecondary(bar, false);
        check(bar.enabled);
        SeekBar real = new SeekBar(new Context(owner));
        PassionBluetoothVolume.refreshSecondary(real, true);
        check(real.enabled);
        PassionBluetoothVolume.refreshSecondary(null, true);
        check(!PassionBluetoothVolume.available(null));
        check(sdk.writes == writes);
        System.out.println(
                "SDK0..11 preserved; absent12 sentinel/no writes; real12 preserved; secondary"
                        + " lifecycle unavailable/restore PASS; synthetic no guest");
    }
}
