package com.qinggan.audiopolicy;

import android.content.Context;
import android.os.Binder;
import android.os.IBinder;

/**
 * The volume groups of the A9 audio policy service as the top curtain, the wheel and BT use them.
 */
public final class AudioPolicyStubServiceTest {
    private static void need(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    private static final class Client extends Binder implements IAudioPolicyInfoCallback {
        int stream = -1;
        int volume = -1;
        int flags = -1;
        int muteChanges;
        boolean muted;

        @Override
        public IBinder asBinder() {
            return this;
        }

        @Override
        public void onAudioPolicyInfoChange(AudioPolicyInfo info) {}

        @Override
        public void onMuteStateChange(boolean muted, int flags) {
            muteChanges++;
            this.muted = muted;
        }

        @Override
        public void onStreamMuteStateChange(int stream, boolean muted) {}

        @Override
        public void onStreamVolumeChange(int stream, int volume, int flags) {
            this.stream = stream;
            this.volume = volume;
            this.flags = flags;
        }
    }

    private static int music() {
        return Context.AUDIO.getStreamVolume(3);
    }

    private static IAudioPolicyService start() {
        AudioPolicyStubService service = new AudioPolicyStubService();

        service.onCreate();

        return (IAudioPolicyService) service.onBind(null);
    }

    private static void groups(IAudioPolicyService audio, Client client) throws Exception {
        // The top curtain sets media stream 3 while BT music (stream 4) holds the policy.
        audio.requestAudioPolicyEx(4, -1, null, null, "bt", "com.qinggan.media", 0, 0);
        audio.setStreamVolume(3, 12, 2, "com.qinggan.app.launcher");
        need(music() == 6, "curtain volume must reach the media mixer: " + music());
        need(
                audio.getStreamVolume(3, "c") == 12 && audio.getStreamVolume(4, "c") == 12,
                "media streams share one volume");
        need(client.stream == 3 && client.volume == 12 && client.flags == 2, "curtain callback");

        // The wheel adjusts the same media volume the curtain reads, reported as stream 3.
        audio.adjustVolume(1, 1024 | 3);
        need(audio.getStreamVolume(3, "c") == 13 && music() == 7, "wheel media volume");
        need(client.stream == 3 && client.volume == 13 && client.flags == 19, "wheel callback");

        // USB music takes the policy back with the same media volume.
        audio.requestAudioPolicyEx(3, -1, null, null, "usb", "com.qinggan.media", 0, 0);
        need(music() == 7 && audio.getStreamVolume(21, "c") == 13, "source change keeps volume");
        audio.setStreamVolume(23, 30, 0, "c");
        need(music() == 15 && audio.getStreamVolume(3, "c") == 30, "media group stream 23");

        // The phone source volume is separate from the media group.
        audio.setBTSourceVolume(5);
        need(audio.getBTSourceVolume() == 5 && audio.getStreamVolume(4, "c") == 30, "BT source");
        need(
                music() == 15 && client.stream == 3 && client.volume == 5 && client.flags == 256,
                "BT source callback");

        // Navigation and phone groups keep their own volume and do not touch the media mixer.
        audio.setStreamVolume(1, 20, 0, "c");
        audio.setStreamVolume(2, 9, 0, "c");
        need(
                audio.getStreamVolume(1, "c") == 20 && audio.getStreamVolume(3, "c") == 30,
                "navigation group");
        need(
                audio.getStreamVolume(0, "c") == 9 && audio.getStreamVolume(24, "c") == 9,
                "phone group");
        need(music() == 15, "other groups leave the media mixer");
    }

    private static void mutes(IAudioPolicyService audio, Client client) throws Exception {
        // A curtain volume change ends the master mute as the OEM Engine does, except with 512.
        audio.setMasterMute(true, 1);
        need(audio.isMasterMute() && music() == 0 && client.muted, "master mute");
        audio.setStreamVolume(3, 10, 512 | 2, "c");
        need(
                audio.isMasterMute() && music() == 0 && audio.getStreamVolume(3, "c") == 10,
                "keep-mute flag");
        audio.setStreamVolume(3, 0, 2, "c");
        need(audio.isMasterMute() && music() == 0, "zero volume keeps the mute");
        int muteChanges = client.muteChanges;
        audio.setStreamVolume(3, 10, 2, "com.qinggan.app.launcher");
        need(!audio.isMasterMute() && !audio.isStreamMute(3) && music() == 5, "curtain unmute");
        need(
                client.muteChanges == muteChanges + 1 && !client.muted && client.volume == 10,
                "unmute callback");
        audio.setMasterMute(true, 1);
        audio.adjustVolume(1, 1024 | 3);
        need(
                !audio.isMasterMute() && music() == 6 && audio.getStreamVolume(3, "c") == 11,
                "wheel unmute");
    }

    public static void main(String[] args) throws Exception {
        IAudioPolicyService audio = start();
        Client client = new Client();

        need(audio.registerAudioPolicyInfoCallback(client, "com.qinggan.app.launcher"), "register");
        groups(audio, client);
        mutes(audio, client);

        // A restarted service reads the stored group volumes.
        IAudioPolicyService restored = start();
        need(
                restored.getStreamVolume(4, "c") == 11 && restored.getBTSourceVolume() == 5,
                "stored volumes");
        System.out.println(
                "AudioPolicyStubServiceTest: PASS curtain, wheel, BT source and stream groups");
    }
}
