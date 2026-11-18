import android.content.Context;
import android.media.AudioManager;

import androidx.preference.PreferenceGroup;

import com.android.car.settings.common.SeekBarPreference;

import com.voyemu.settings.PassionSoundStreams;

public class PassionSoundStreamsTest {
    static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    public static void main(String[] args) {
        Context context = new Context();
        AudioManager audio =
                new AudioManager() {
                    @Override
                    public int getStreamMinVolume(int stream) {
                        return stream == 3 ? 1 : 0;
                    }

                    @Override
                    public int getStreamMaxVolume(int stream) {
                        return stream == 3 ? 9 : 10;
                    }

                    @Override
                    public void setStreamVolume(int stream, int value, int flags) {
                        super.setStreamVolume(stream, value, flags);

                        if (stream == 2) {
                            values[5] = value;
                        }
                    }
                };

        context.audio = audio;
        PreferenceGroup group = new PreferenceGroup(context);
        audio.values[3] = 4;
        audio.values[2] = 5;
        PassionSoundStreams.update(group);
        check(context.registrations == 1 && group.items.size() == 5);
        SeekBarPreference media = (SeekBarPreference) group.items.get("voyemu_android_stream_3");
        SeekBarPreference ring = (SeekBarPreference) group.items.get("voyemu_android_stream_2");
        check(media.value == 4 && media.min == 1 && media.max == 9 && ring.value == 5);
        check(
                media.listener.onPreferenceChange(media, 7)
                        && audio.values[3] == 7
                        && media.value == 7);
        check(
                !media.listener.onPreferenceChange(media, 0)
                        && !media.listener.onPreferenceChange(media, 10)
                        && audio.values[3] == 7);
        SeekBarPreference notification =
                (SeekBarPreference) group.items.get("voyemu_android_stream_5");
        check(ring.listener.onPreferenceChange(ring, 6) && notification.value == 6);
        audio.values[3] = 2;
        audio.values[2] = 2;
        context.broadcast();
        check(media.value == 2 && ring.value == 2);
        PassionSoundStreams.update(group);
        check(context.registrations == 1);
        PreferenceGroup other = new PreferenceGroup(context);
        PassionSoundStreams.update(other);
        check(context.registrations == 2);
        PassionSoundStreams.dispose(other);
        check(context.unregistrations == 1);
        PassionSoundStreams.dispose(group);
        PassionSoundStreams.dispose(group);
        check(context.unregistrations == 2 && context.receiver == null);
        audio.values[3] = 9;
        context.broadcast();
        check(media.value == 2);
        System.out.println("PassionSoundStreams behavioral test PASS");
    }
}
