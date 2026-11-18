package com.voyemu.input;

import android.os.Handler;

import com.qinggan.audiopolicy.AudioPolicyInfo;
import com.qinggan.audiopolicy.AudioPolicyStubService;
import com.qinggan.audiopolicy.IAudioPolicyInfoCallback;
import com.qinggan.audiopolicy.IAudioPolicyService;
import com.voyemu.oem.KeyManagerStub;

/** Runs the actual audio component and wheel terminal against synthetic Android transport. */
public final class WheelVolumeHostProbe {
    static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    static void rejects(Runnable action) {
        boolean failed = false;

        try {
            action.run();
        } catch (IllegalArgumentException e) {
            failed = true;
        }

        check(failed);
    }

    static final class Client extends android.os.Binder implements IAudioPolicyInfoCallback {
        int calls, stream, volume, flags, muteCalls;
        boolean fail;
        IAudioPolicyService service;
        final java.util.List<Integer> muteStreams = new java.util.ArrayList<>();

        public void onMuteStateChange(boolean muted, int flags) {
            check(service.isMasterMute() == muted && flags == 1);
            muteCalls++;
        }

        public void onStreamMuteStateChange(int stream, boolean muted) {
            check(service.isStreamMute(stream) == muted);
            muteStreams.add(stream);
        }

        public android.os.IBinder asBinder() {
            return this;
        }

        public void onAudioPolicyInfoChange(AudioPolicyInfo info) {}

        public void onStreamVolumeChange(int stream, int volume, int flags)
                throws android.os.RemoteException {
            if (fail) {
                throw new android.os.RemoteException();
            }

            java.util.concurrent.atomic.AtomicInteger queried =
                    new java.util.concurrent.atomic.AtomicInteger(-1);
            Thread peer = new Thread(() -> queried.set(service.getStreamVolume(stream, "client")));
            peer.setDaemon(true);
            peer.start();

            try {
                peer.join(1000);
            } catch (InterruptedException e) {
                throw new AssertionError(e);
            }

            check(
                    !peer.isAlive()
                            && queried.get()
                                    == volume); // A separate thread must not wait for a held state
            // lock.
            calls++;
            this.stream = stream;
            this.volume = volume;
            this.flags = flags;
        }
    }

    public static final class PrivatePolicyInfo {
        private final int streamType = 4;

        public int getStreamType() {
            return streamType;
        }
    }

    private static int volumes(IAudioPolicyService audio, KeyManagerStub wheel, Client client)
            throws Exception {
        KeyEventRouter.Delivery up = wheel.dispatchQG(2, 8192);

        Handler.flush();
        check(
                up.terminals == 1
                        && up.consumed
                        && up.volume.before == 15
                        && up.volume.after == 16
                        && up.volume.stream == 3);
        check(client.calls == 1 && client.stream == 3 && client.volume == 16 && client.flags == 19);
        check(android.media.AudioManager.current == 8);
        check(
                wheel.dispatchQG(2, 65536).terminals == 0
                        && audio.getStreamVolume(3, "client") == 16);
        KeyEventRouter.Delivery down = wheel.dispatchQG(1, 8192);
        Handler.flush();
        check(
                down.volume.before == 16
                        && down.volume.after == 15
                        && client.calls == 2
                        && client.volume == 15);
        // Actual active media stream changes the adjustment target, not a wheel-local counter.
        audio.requestAudioPolicy(4, 0, null, null, "bt", "com.qinggan.media", 0);
        wheel.dispatchQG(2, 8192);
        Handler.flush();
        check(
                audio.getStreamVolume(4, "client") == 16
                        && audio.getStreamVolume(3, "client") == 15
                        && client.stream == 4);
        audio.adjustStreamVolume(3, 1, 2, 1027);
        Handler.flush();
        check(audio.getStreamVolume(3, "client") == 17);
        check(
                android.media.AudioManager.current
                        == 8); // Inactive USB must not change active BT gain.
        audio.requestAudioPolicy(3, 0, null, null, "usb", "com.qinggan.media", 0);
        check(android.media.AudioManager.current == 9);
        audio.setStreamVolume(3, 999, 1027, "client");
        Handler.flush();
        check(audio.getStreamVolume(3, "client") == 30);
        check(android.media.AudioManager.current == 15);
        audio.adjustStreamVolume(3, 1, Integer.MAX_VALUE, 1027);
        Handler.flush();
        check(audio.getStreamVolume(3, "client") == 30);
        audio.setStreamVolume(3, -100, 1027, "client");
        Handler.flush();
        check(audio.getStreamVolume(3, "client") == 0);
        check(android.media.AudioManager.current == 0);
        audio.adjustStreamVolume(3, -1, Integer.MAX_VALUE, 1027);
        Handler.flush();
        check(audio.getStreamVolume(3, "client") == 0);
        // Match the actual OEM Engine callback ABI, including display target and no-UI updates.
        audio.setStreamVolume(3, 7, 1 | 32 | 2 | 1024, "client");
        Handler.flush();
        check(client.flags == 35);
        audio.setStreamVolume(3, 7, 1 | 256 | 1024, "client");
        Handler.flush();
        check(client.flags == 273);
        audio.setStreamVolume(3, 7, 32 | 256 | 1024, "client");
        Handler.flush();
        check(client.flags == 256);
        audio.setStreamVolume(3, 7, 0, "client");
        Handler.flush();
        check(client.flags == 0);
        int stableVolume = audio.getStreamVolume(3, "client"),
                stableMixer = android.media.AudioManager.current;
        android.media.AudioManager.fail = true;
        boolean mixerFailed = false;

        try {
            audio.setStreamVolume(3, stableVolume + 1, 0, "client");
        } catch (IllegalStateException expected) {
            mixerFailed = true;
        }

        check(mixerFailed);
        android.media.AudioManager.fail = false;
        Handler.flush();
        check(
                audio.getStreamVolume(3, "client") == stableVolume
                        && android.media.AudioManager.current == stableMixer);
        audio.setStreamVolume(4, 16, 0, "client");
        Handler.flush();
        check(android.media.AudioManager.current == stableMixer);
        audio.requestAudioPolicy(4, 0, null, null, "bt", "com.qinggan.media", 0);
        check(android.media.AudioManager.current == 8);
        audio.requestAudioPolicy(3, 0, null, null, "usb", "com.qinggan.media", 0);
        check(android.media.AudioManager.current == stableMixer);

        return stableMixer;
    }

    private static void mutes(
            IAudioPolicyService audio, KeyManagerStub wheel, Client client, int stableMixer)
            throws Exception {
        int savedVolume = audio.getStreamVolume(3, "client");
        KeyEventRouter.Delivery mute = wheel.dispatchQG(1, 32768);

        Handler.flush();
        check(mute.consumed && mute.terminals == 1 && audio.isMasterMute());
        check(client.muteCalls == 1 && client.muteStreams.equals(java.util.Arrays.asList(1, 3, 4)));
        check(
                audio.isStreamMute(1)
                        && audio.isStreamMute(3)
                        && audio.isStreamMute(4)
                        && !audio.isStreamMute(2));
        check(
                android.media.AudioManager.current == 0
                        && audio.getStreamVolume(3, "client") == savedVolume);
        check(
                wheel.dispatchQG(1, 524288).terminals == 0
                        && wheel.dispatchQG(1, 65536).terminals == 0);
        check(audio.isMasterMute());
        audio.requestAudioPolicy(4, 0, null, null, "bt", "com.qinggan.media", 0);
        check(
                android.media.AudioManager.current
                        == 0); // Changing media source must not defeat mute.
        audio.requestAudioPolicy(3, 0, null, null, "usb", "com.qinggan.media", 0);
        check(wheel.dispatchQG(1, 32768).consumed && !audio.isMasterMute());
        check(!audio.isStreamMute(1) && !audio.isStreamMute(3) && !audio.isStreamMute(4));
        check(
                android.media.AudioManager.current == stableMixer
                        && audio.getStreamVolume(3, "client") == savedVolume);
        Handler.flush();
        check(
                client.muteCalls == 2
                        && client.muteStreams.equals(java.util.Arrays.asList(1, 3, 4, 1, 3, 4)));
        android.media.AudioManager.fail = true;
        KeyEventRouter.Delivery muteFailed = wheel.dispatchQG(1, 32768);
        check(
                !muteFailed.consumed
                        && muteFailed.terminals == 0
                        && muteFailed.terminalFailure.equals("method-exception"));
        check(
                !audio.isMasterMute()
                        && !audio.isStreamMute(1)
                        && audio.getStreamVolume(3, "client") == savedVolume);
        android.media.AudioManager.fail = false;
        Handler.flush();
        check(client.muteCalls == 2); // No OEM notification on failed backend write.
    }

    private static void failures(IAudioPolicyService audio, KeyManagerStub wheel, Client client)
            throws Exception {
        int beforeCalls = client.calls;

        rejects(() -> audio.adjustStreamVolume(3, 2, 1, 0));
        rejects(() -> audio.adjustStreamVolume(3, 1, -1, 0));
        rejects(() -> audio.getStreamVolume(25, "client"));
        Handler.flush();
        check(client.calls == beforeCalls);
        // Unregister cancels pending callbacks; a dead remote removes its registration/death link.
        audio.adjustStreamVolume(3, 1, 1, 1027);
        check(audio.unregisterAudioPolicyInfoCallback(client));
        Handler.flush();
        check(client.calls == beforeCalls && client.links.isEmpty());
        Client dead = new Client();
        dead.service = audio;
        dead.fail = true;
        audio.registerAudioPolicyInfoCallback(dead, "dead");
        Handler.flush();
        audio.adjustStreamVolume(3, 1, 1, 1027);
        Handler.flush();
        check(dead.links.isEmpty());
        android.content.SharedPreferences.fail = true;
        KeyEventRouter.Delivery failedTerminal = wheel.dispatchQG(2, 8192);
        check(
                failedTerminal.terminals == 0
                        && !failedTerminal.consumed
                        && failedTerminal.terminalFailure.equals("method-exception"));
        check(KeyManagerStub.deliveryStatus(failedTerminal).equals("terminal-unavailable"));
        check(failedTerminal.terminalDiagnostics.get("operation").equals("adjust-volume"));
        check(
                failedTerminal
                        .terminalDiagnostics
                        .get("cause_class")
                        .equals("java.lang.IllegalStateException"));
        KeyEventRouter.Delivery aggregate = new KeyEventRouter.Delivery();
        aggregate.add(failedTerminal);
        aggregate.add(wheel.dispatchQG(2, 65536));
        check(aggregate.terminalFailure.equals("method-exception"));
        check(KeyManagerStub.deliveryStatus(aggregate).equals("terminal-unavailable"));
        int current = audio.getStreamVolume(3, "client");
        boolean failed = false;

        try {
            audio.adjustStreamVolume(3, 1, 1, 1027);
        } catch (IllegalStateException expected) {
            failed = true;
        }

        check(failed && audio.getStreamVolume(3, "client") == current);
        check(android.media.AudioManager.current == (int) Math.round(current * 15d / 30d));
        android.content.SharedPreferences.fail = false;
        AudioPolicyStubService restarted = new AudioPolicyStubService();
        restarted.onCreate();
        IAudioPolicyService restored = (IAudioPolicyService) restarted.onBind(null);
        check(
                restored.getStreamVolume(3, "client") == current
                        && restored.getStreamVolume(4, "client") == 16);
        System.out.println("PASS OEM wheel volume service queries/callbacks");
    }

    public static void main(String[] args) throws Exception {
        PrivatePolicyInfo privateInfo = new PrivatePolicyInfo();
        boolean publicFieldMissing = false;

        try {
            privateInfo.getClass().getField("streamType");
        } catch (NoSuchFieldException expected) {
            publicFieldMissing = true;
        }

        check(publicFieldMissing && AudioControl.policyStream(privateInfo) == 4);
        check(AudioControl.policyStream(null) == 3);
        AudioPolicyStubService actual = new AudioPolicyStubService();
        actual.onCreate();
        check(android.media.AudioManager.current == 0 && android.media.AudioManager.lastFlags == 0);
        IAudioPolicyService audio = (IAudioPolicyService) actual.onBind(null);
        android.content.Context.audioBinder = null;
        AudioControl waiting = new AudioControl(new android.content.Context());
        check(waiting.adjust(1) == null);
        check(waiting.diagnostics().get("failure").equals("bind-rejected"));
        check(waiting.diagnostics().get("connected_callbacks").equals(0));
        android.content.Context.audioBinder = actual.onBind(null);
        Handler.async = true;
        Thread connect =
                new Thread(() -> {
                    try {
                        Thread.sleep(100);
                    } catch (InterruptedException e) {
                        throw new AssertionError(e);
                    }

                    Handler.flush();
                });
        connect.start();
        AudioControl.VolumeChange recovered = waiting.adjust(1);
        connect.join(2000);
        check(recovered != null && recovered.before == 0 && recovered.after == 1);
        check(waiting.diagnostics().get("client_ready").equals(true));
        check(waiting.diagnostics().get("connected_callbacks").equals(1));
        check(waiting.diagnostics().get("failure").equals("none"));
        check(
                waiting.diagnostics()
                        .get("policy_info_class")
                        .equals("com.qinggan.audiopolicy.AudioPolicyInfo"));
        Handler.async = false;
        audio.setStreamVolume(3, 15, 0, "test");
        audio.setStreamVolume(4, 15, 0, "test");
        Handler.flush();
        android.content.Context.failLoader = true;
        AudioControl badLoader = new AudioControl(new android.content.Context());
        check(badLoader.adjust(1) == null);
        check(badLoader.diagnostics().get("failure").equals("callback-failed"));
        check(badLoader.diagnostics().get("operation").equals("get-class-loader"));
        check(badLoader.diagnostics().get("connected_callbacks").equals(1));
        check(badLoader.diagnostics().get("error_class").equals("java.lang.IllegalStateException"));
        android.content.Context.failLoader = false;
        Client client = new Client();
        client.service = audio;
        check(audio.registerAudioPolicyInfoCallback(client, "client"));
        Handler.flush();
        check(audio.getStreamVolume(3, "client") == 15 && audio.getStreamMaxVolume(3) == 30);
        KeyManagerStub wheel = new KeyManagerStub();
        wheel.attach(new android.content.Context());
        check(wheel.dispatchQG(2, 4096).terminals == 0 && audio.getStreamVolume(3, "client") == 15);
        int stableMixer = volumes(audio, wheel, client);
        mutes(audio, wheel, client, stableMixer);
        failures(audio, wheel, client);
    }
}
