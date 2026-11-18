package com.voyemu.input;

/** Executes materialized production reflection calls against the NXP ABI. */
public final class NxpWheelAudioProbe {
    public static final class Policy {
        public int getStreamType() {
            return 7;
        }
    }

    public static final class Mixer {
        int volume = 12, current, last, reads, writes;
        boolean fallback;

        public Policy getCurrentAudioPolicyInfo(int channel) {
            check(channel == 0);
            current++;

            return fallback ? null : new Policy();
        }

        public Policy getLastMediaAudioPolicyInfoByChannel(int channel) {
            check(channel == 0);
            last++;

            return new Policy();
        }

        public int getStreamVolume(int stream) {
            check(stream == 7);
            reads++;

            return volume;
        }

        public void adjustVolume(int direction, int flags) {
            check((direction == 1 || direction == -1) && flags == 1027);
            writes++;
            volume += direction;
        }
    }

    public static final class WrongAbi {
        public Policy getCurrentAudioPolicyInfo(int channel, String owner) {
            return new Policy();
        }

        public int getStreamVolume(int stream, String owner) {
            return 12;
        }

        public void adjustVolume(int direction, int flags) {
            throw new AssertionError("wrong ABI wrote");
        }
    }

    private static void check(boolean value) {
        if (!value) {
            throw new AssertionError("NXP audio ABI/readback");
        }
    }

    private static AudioControl bound(Object mixer) throws Exception {
        AudioControl control = new AudioControl(new android.content.Context());
        var service = AudioControl.class.getDeclaredField("service");

        service.setAccessible(true);
        service.set(control, mixer);
        var api = AudioControl.class.getDeclaredField("api");
        api.setAccessible(true);
        api.set(control, mixer.getClass());

        return control;
    }

    public static void main(String[] args) throws Exception {
        Mixer mixer = new Mixer();
        AudioControl control = bound(mixer);

        if (args[0].equals("before")) {
            try {
                control.adjust(1);
            } catch (NoSuchMethodException expected) {
                return;
            }

            throw new AssertionError("Unadapted A9 ABI unexpectedly accepted NXP");
        }

        var up = control.adjust(1);
        check(up.stream == 7 && up.before == 12 && up.after == 13);
        mixer.fallback = true;
        var down = control.adjust(-1);
        check(down.before == 13 && down.after == 12);
        check(mixer.current == 2 && mixer.last == 1 && mixer.reads == 4 && mixer.writes == 2);

        try {
            bound(new WrongAbi()).adjust(1);
        } catch (NoSuchMethodException expected) {
            return;
        }

        throw new AssertionError("A9 String ABI accepted after NXP adaptation");
    }
}
