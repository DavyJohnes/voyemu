package com.voyemu.input;

/** Finite platform MEDIA terminal, with real mixer readback and no OEM audio-policy simulation. */
public final class FreeWheelVolume {
    public interface Mixer {
        int mode();

        boolean bluetoothSco();

        int minimum();

        int maximum();

        int volume();

        boolean muted();

        void adjust(int direction);

        void mute(boolean value);
    }

    public static final class Result {
        public final String status;
        public final int before, after;
        public final Boolean mutedBefore, mutedAfter;

        Result(String status, int before, int after, Boolean mutedBefore, Boolean mutedAfter) {
            this.status = status;
            this.before = before;
            this.after = after;
            this.mutedBefore = mutedBefore;
            this.mutedAfter = mutedAfter;
        }
    }

    private static Result unsupported() {
        return new Result("unsupported-call-audio", -1, -1, null, null);
    }

    private static void range(int minimum, int maximum, int volume) {
        if (minimum < 0 || maximum <= minimum || volume < minimum || volume > maximum) {
            throw new IllegalStateException("media-volume-range");
        }
    }

    public static Result adjust(Mixer mixer, int direction) {
        if (direction != -1 && direction != 1) {
            throw new IllegalArgumentException("media-volume-direction");
        }

        if (mixer.mode() != 0 || mixer.bluetoothSco()) {
            return unsupported();
        }

        int minimum = mixer.minimum(), maximum = mixer.maximum(), before = mixer.volume();
        range(minimum, maximum, before);
        boolean muted = mixer.muted();
        int expected = (int) Math.max(minimum, Math.min(maximum, (long) before + direction));
        mixer.adjust(direction);
        int after = mixer.volume();
        range(minimum, maximum, after);

        if (after != expected) {
            throw new IllegalStateException("media-volume-readback");
        }

        return new Result(
                after == before ? "boundary" : "applied", before, after, muted, mixer.muted());
    }

    public static Result mute(Mixer mixer, boolean wanted) {
        if (mixer.mode() != 0 || mixer.bluetoothSco()) {
            return unsupported();
        }

        int before = mixer.volume();
        range(mixer.minimum(), mixer.maximum(), before);
        boolean previous = mixer.muted();

        if (wanted != previous) {
            mixer.mute(wanted);
        }

        boolean actual = mixer.muted();
        int after = mixer.volume();
        range(mixer.minimum(), mixer.maximum(), after);

        if (actual != wanted) {
            throw new IllegalStateException("media-mute-readback");
        }

        return new Result(
                previous == wanted ? "unchanged" : "applied", before, after, previous, actual);
    }

    private FreeWheelVolume() {}
}
