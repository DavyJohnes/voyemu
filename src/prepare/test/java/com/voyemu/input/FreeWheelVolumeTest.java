package com.voyemu.input;

/**
 * Real terminal policy with a deterministic mixer fixture; actual device audio remains a guest
 * gate.
 */
public final class FreeWheelVolumeTest {
    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static final class Mixer implements FreeWheelVolume.Mixer {
        int mode, volume = 5, maximum = 10, minimum, writes;
        boolean sco, muted, ignore;

        @Override
        public int mode() {
            return mode;
        }

        @Override
        public boolean bluetoothSco() {
            return sco;
        }

        @Override
        public int minimum() {
            return minimum;
        }

        @Override
        public int maximum() {
            return maximum;
        }

        @Override
        public int volume() {
            return volume;
        }

        @Override
        public boolean muted() {
            return muted;
        }

        @Override
        public void adjust(int direction) {
            writes++;

            if (!ignore) {
                volume = Math.max(minimum, Math.min(maximum, volume + direction));
            }
        }

        @Override
        public void mute(boolean value) {
            writes++;

            if (!ignore) {
                muted = value;
            }
        }
    }

    private interface Action {
        void run();
    }

    private static void rejected(Action action) {
        try {
            action.run();
        } catch (IllegalArgumentException | IllegalStateException expected) {
            return;
        }

        throw new AssertionError("invalid mixer result acknowledged");
    }

    public static void main(String[] args) {
        Mixer mixer = new Mixer();
        var up = FreeWheelVolume.adjust(mixer, 1);

        check(
                up.status.equals("applied") && up.before == 5 && up.after == 6 && mixer.writes == 1,
                "real step/readback");
        check(FreeWheelVolume.adjust(mixer, -1).after == 5, "real decrease");
        mixer.volume = 10;
        check(
                FreeWheelVolume.adjust(mixer, 1).status.equals("boundary"),
                "boundary is not a fabricated change");
        check(FreeWheelVolume.mute(mixer, true).mutedAfter && mixer.muted, "real media mute");
        int writes = mixer.writes;
        check(
                FreeWheelVolume.mute(mixer, true).status.equals("unchanged")
                        && mixer.writes == writes,
                "idempotent mute");
        check(!FreeWheelVolume.mute(mixer, false).mutedAfter, "unmute");
        mixer.mode = 2;
        writes = mixer.writes;
        check(
                FreeWheelVolume.adjust(mixer, 1).status.equals("unsupported-call-audio"),
                "no telephony-volume guess");
        check(
                FreeWheelVolume.mute(mixer, true).status.equals("unsupported-call-audio")
                        && mixer.writes == writes,
                "no call mute");
        mixer.mode = 0;
        mixer.sco = true;
        check(
                FreeWheelVolume.adjust(mixer, -1).status.equals("unsupported-call-audio")
                        && mixer.writes == writes,
                "SCO no writes");
        mixer.sco = false;
        mixer.volume = 5;
        mixer.ignore = true;
        rejected(() -> FreeWheelVolume.adjust(mixer, 1));
        rejected(() -> FreeWheelVolume.mute(mixer, true));
        mixer.ignore = false;
        mixer.maximum = 0;
        writes = mixer.writes;
        rejected(() -> FreeWheelVolume.adjust(mixer, 1));
        check(mixer.writes == writes, "bad range no write");
        mixer.maximum = 10;
        rejected(() -> FreeWheelVolume.adjust(mixer, 0));
        System.out.println(
                "Free finite platform MEDIA adjust/mute/readback/unsupported policy PASS; no guest"
                        + " audio proof");
    }
}
