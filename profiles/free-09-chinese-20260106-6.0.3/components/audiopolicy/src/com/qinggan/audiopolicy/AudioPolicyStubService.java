package com.qinggan.audiopolicy;

import android.app.Service;
import android.content.Intent;
import android.media.AudioManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.RemoteException;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Emulator stub for the OEM "qg.audio_policy" binder service.
 *
 * <p>Clients (Launcher, com.qinggan.app.music, com.qinggan.media) first try
 * ServiceManager.getService("qg.audio_policy") and then fall back to bindService(action
 * "com.qinggan.audiopolicy.action.AUDIO_POLICY_SERVICE", package "com.qinggan.audiopolicy.service")
 * — this APK provides that service and returns an IAudioPolicyService binder from onBind().
 *
 * <p>Purpose: make BT / USB Music audio sources selectable+switchable in the launcher music widget
 * and the Music app by granting every media request (streamType 3=USB/media, 4=BT) and reporting it
 * as the current/last media client.
 */
public class AudioPolicyStubService extends Service {
    private static final String TAG = "AudioPolicyStub";

    /** Copyable state: the last granted media policy. */
    private final Object mLock = new Object();

    private final Object mVolumeWrites = StartupAudioPolicy.class;
    private AudioPolicyInfo mCurrInfo;
    private AudioPolicyInfo mLastMediaInfo = defaultMediaInfo();

    /**
     * Cold-start default: last media source = Kuwo, so clients see a valid source before any focus
     * request.
     */
    private static AudioPolicyInfo defaultMediaInfo() {
        AudioPolicyInfo info = new AudioPolicyInfo();

        info.packageName = "com.qinggan.media";
        info.streamType = 3;
        info.clientId = "com.qinggan.media.kwmusic";

        return info;
    }

    private final Map<IBinder, IAudioPolicyInfoCallback> mInfoCallbacks = new HashMap<>();
    private final Map<IBinder, IBinder.DeathRecipient> mInfoDeaths = new HashMap<>();
    private final Map<IBinder, IBTSourceConnected> mBtConnectedCallbacks = new HashMap<>();
    private final Map<IBinder, IBinder.DeathRecipient> mBtDeaths = new HashMap<>();
    private final Handler mHandler = new Handler(Looper.getMainLooper());

    private final IAudioPolicyService.Stub mBinder =
            new IAudioPolicyService.Stub() {

                // ---- request / abandon (the core of the emulation) ----

                @Override
                public int requestAudioPolicy(
                        int streamType,
                        int channel,
                        IBinder token,
                        IAudioPolicyCallback cb,
                        String clientId,
                        String packageName,
                        int flags) {
                    android.util.Log.i(
                            TAG,
                            "requestAudioPolicy streamType="
                                    + streamType
                                    + " clientId="
                                    + clientId
                                    + " pkg="
                                    + packageName);

                    return handleRequest(streamType, clientId, packageName);
                }

                @Override
                public int requestAudioPolicyEx(
                        int streamType,
                        int channel,
                        IBinder token,
                        IAudioPolicyCallback cb,
                        String clientId,
                        String packageName,
                        int flags,
                        int ext) {
                    android.util.Log.i(
                            TAG,
                            "requestAudioPolicyEx streamType="
                                    + streamType
                                    + " clientId="
                                    + clientId
                                    + " pkg="
                                    + packageName
                                    + " ext="
                                    + ext);

                    return handleRequest(streamType, clientId, packageName);
                }

                private int handleRequest(int streamType, String clientId, String packageName) {
                    ensureStartupAudio();

                    // 1 == AUDIO_POLICY_GRANTED. Grant everything; only track media streams.
                    if (streamType == 3 || streamType == 4) {
                        AudioPolicyInfo info = new AudioPolicyInfo();
                        info.packageName = packageName == null ? "" : packageName;
                        info.streamType = streamType;
                        info.clientId = clientId == null ? "" : clientId;

                        synchronized (mVolumeWrites) {
                            applyPlatformMediaVolume(streamType, readVolume(streamType));

                            synchronized (mLock) {
                                mCurrInfo = info;
                                mLastMediaInfo = info;
                            }
                        }

                        notifyInfoCallbacks(info);
                    }

                    return 1;
                }

                @Override
                public int abandonAudioPolicy(
                        IAudioPolicyCallback cb, String clientId, boolean flag) {
                    android.util.Log.i(TAG, "abandonAudioPolicy clientId=" + clientId);

                    synchronized (mLock) {
                        if (mCurrInfo != null
                                && clientId != null
                                && clientId.equals(mCurrInfo.clientId)) {
                            mCurrInfo = null;
                        }
                    }

                    return 1;
                }

                @Override
                public int requestAudioFocus(
                        IAudioPolicyCallback cb,
                        String clientId,
                        int value,
                        int value2,
                        String text,
                        int value3,
                        int value4) {
                    return 1; // granted
                }

                @Override
                public int abandonAudioFocus(
                        IAudioPolicyCallback cb, String clientId, String text, boolean flag) {
                    return 1;
                }

                // ---- current / last media info queries ----

                @Override
                public AudioPolicyInfo getLastMediaAudioPolicyInfoByChannel(int channel) {
                    synchronized (mLock) {
                        return copy(mLastMediaInfo);
                    }
                }

                @Override
                public AudioPolicyInfo getCurrMediaAudioPolicyInfoByChannel(int channel) {
                    synchronized (mLock) {
                        return copy(mLastMediaInfo);
                    }
                }

                @Override
                public AudioPolicyInfo getCurrAudioPolicyInfoByChannel(int channel) {
                    synchronized (mLock) {
                        return copy(mCurrInfo);
                    }
                }

                @Override
                public AudioPolicyInfo getCurrentAudioPolicyInfo(int value, String text) {
                    synchronized (mLock) {
                        return copy(mCurrInfo);
                    }
                }

                @Override
                public AudioPolicyInfo getA2dpAudioPolicyInfo(String text) {
                    synchronized (mLock) {
                        return copy(mCurrInfo);
                    }
                }

                @Override
                public List<AudioPolicyInfo> getAudioPolicyInfo(int value) {
                    synchronized (mLock) {
                        return mCurrInfo == null
                                ? Collections.<AudioPolicyInfo>emptyList()
                                : Collections.singletonList(copy(mCurrInfo));
                    }
                }

                // ---- registration callbacks ----

                @Override
                public boolean registerAudioPolicyInfoCallback(
                        final IAudioPolicyInfoCallback cb, String text) {
                    if (cb == null) {
                        return false;
                    }

                    final IBinder binder = cb.asBinder();
                    final IBinder.DeathRecipient recipient;

                    synchronized (mInfoCallbacks) {
                        if (mInfoDeaths.containsKey(binder)) {
                            return true;
                        }

                        recipient =
                                new IBinder.DeathRecipient() {
                                    @Override
                                    public void binderDied() {
                                        synchronized (mInfoCallbacks) {
                                            if (mInfoDeaths.get(binder) != this) {
                                                return;
                                            }

                                            mInfoDeaths.remove(binder);
                                            mInfoCallbacks.remove(binder);
                                        }
                                    }
                                };

                        try {
                            binder.linkToDeath(recipient, 0);
                        } catch (RemoteException e) {
                            return false;
                        }

                        mInfoDeaths.put(binder, recipient);
                        mInfoCallbacks.put(binder, cb);
                    }

                    final AudioPolicyInfo snapshot;

                    synchronized (mLock) {
                        snapshot = copy(mCurrInfo != null ? mCurrInfo : mLastMediaInfo);
                    }

                    mHandler.postDelayed(
                            new Runnable() {
                                @Override
                                public void run() {
                                    synchronized (mInfoCallbacks) {
                                        if (mInfoDeaths.get(binder) != recipient) {
                                            return;
                                        }
                                    }

                                    try {
                                        cb.onAudioPolicyInfoChange(snapshot);
                                    } catch (RemoteException ignored) {
                                    }
                                }
                            },
                            300);

                    return true;
                }

                @Override
                public boolean unregisterAudioPolicyInfoCallback(IAudioPolicyInfoCallback cb) {
                    if (cb == null) {
                        return false;
                    }

                    synchronized (mInfoCallbacks) {
                        IBinder binder = cb.asBinder();
                        IBinder.DeathRecipient recipient = mInfoDeaths.remove(binder);

                        if (recipient != null) {
                            binder.unlinkToDeath(recipient, 0);
                        }

                        return mInfoCallbacks.remove(binder) != null;
                    }
                }

                @Override
                public boolean registerBTSourceConnected(final IBTSourceConnected cb, String text) {
                    if (cb == null) {
                        return false;
                    }

                    final IBinder binder = cb.asBinder();
                    final IBinder.DeathRecipient recipient;

                    synchronized (mBtConnectedCallbacks) {
                        if (mBtDeaths.containsKey(binder)) {
                            return true;
                        }

                        recipient =
                                new IBinder.DeathRecipient() {
                                    @Override
                                    public void binderDied() {
                                        synchronized (mBtConnectedCallbacks) {
                                            if (mBtDeaths.get(binder) != this) {
                                                return;
                                            }

                                            mBtDeaths.remove(binder);
                                            mBtConnectedCallbacks.remove(binder);
                                        }
                                    }
                                };

                        try {
                            binder.linkToDeath(recipient, 0);
                        } catch (RemoteException e) {
                            return false;
                        }

                        mBtDeaths.put(binder, recipient);
                        mBtConnectedCallbacks.put(binder, cb);
                    }

                    mHandler.postDelayed(
                            new Runnable() {
                                @Override
                                public void run() {
                                    synchronized (mBtConnectedCallbacks) {
                                        if (mBtDeaths.get(binder) != recipient) {
                                            return;
                                        }
                                    }

                                    try {
                                        cb.onConnected(true);
                                    } catch (RemoteException ignored) {
                                    }
                                }
                            },
                            500);

                    return true;
                }

                @Override
                public boolean unregisterBTSourceConnected(IBTSourceConnected cb) {
                    if (cb == null) {
                        return false;
                    }

                    synchronized (mBtConnectedCallbacks) {
                        IBinder binder = cb.asBinder();
                        IBinder.DeathRecipient recipient = mBtDeaths.remove(binder);

                        if (recipient != null) {
                            binder.unlinkToDeath(recipient, 0);
                        }

                        return mBtConnectedCallbacks.remove(binder) != null;
                    }
                }

                @Override
                public boolean registerBTSourceInfoCallback(IBTSourceInfoCallback cb, String text) {
                    android.util.Log.i(TAG, "registerBTSourceInfoCallback pkg=" + text);

                    return cb != null;
                }

                @Override
                public boolean unregisterBTSourceInfoCallback(IBTSourceInfoCallback cb) {
                    return true;
                }

                @Override
                public boolean registerAudioClientCallback(IAudioClientCallback cb, String text) {
                    return cb != null;
                }

                @Override
                public boolean unregisterAudioClientCallback(IAudioClientCallback cb) {
                    return true;
                }

                @Override
                public boolean registerAudioExceptionCallback(IAudioExceptionCallback cb) {
                    return cb != null;
                }

                @Override
                public boolean unregisterAudioExceptionCallback(IAudioExceptionCallback cb) {
                    return true;
                }

                @Override
                public void registerSoundChannelForPkg(String text, int value) {}

                @Override
                public void unregisterSoundChannelForPkg(String text) {}

                @Override
                public void unregisterAudioPolicyClient(String text) {}

                // ---- state queries ----

                @Override
                public boolean isBTSourceConnected() {
                    return true;
                }

                @Override
                public boolean isInIVoka() {
                    return false;
                }

                @Override
                public boolean isInCall(String text) {
                    return false;
                }

                @Override
                public boolean isMediaPlaying() {
                    return false;
                }

                @Override
                public List<AudioClient> getAudioClients() {
                    return Collections.<AudioClient>emptyList();
                }

                @Override
                public int getStreamVolume(int stream, String pkg) {
                    return readVolume(stream);
                }

                @Override
                public int getStreamMaxVolume(int stream) {
                    validateStream(stream);

                    return 30;
                }

                @Override
                public int getBTSourceVolume() {
                    synchronized (mLock) {
                        return storedVolume(BT_SOURCE_KEY);
                    }
                }

                // ---- benign defaults for everything else ----

                @Override
                public void adjustStreamVolume(int stream, int direction, int step, int flags) {
                    adjust(stream, direction, step, flags);
                }

                @Override
                public void adjustVolume(int direction, int flags) {
                    // OEM Engine.adjustVolume adjusts a BT (4) or 21 source as media stream 3.
                    int stream = activeStream();

                    adjust(stream == 4 || stream == 21 ? 3 : stream, direction, 1, flags);
                }

                @Override
                public int getArkarmysMode() {
                    return 0;
                }

                @Override
                public int getAtmosMode() {
                    return 0;
                }

                @Override
                public int[] getBalanceFaderLevel() {
                    return new int[] {0, 0};
                }

                @Override
                public int getBandValue(int value) {
                    return 0;
                }

                @Override
                public int getBassFreq() {
                    return 0;
                }

                @Override
                public int getCanDspType() {
                    return 0;
                }

                @Override
                public int getChannel() {
                    return 0;
                }

                @Override
                public int getChimeMode() {
                    return 0;
                }

                @Override
                public int getDtsMode() {
                    return 0;
                }

                @Override
                public int[] getEQDefault() {
                    return new int[] {0, 0, 0, 0, 0};
                }

                @Override
                public AudioEQMode getEQMode() {
                    return null;
                }

                @Override
                public AudioEQMode getEQModeByIndex(int value) {
                    return null;
                }

                @Override
                public int getEQModeIndex() {
                    return 0;
                }

                @Override
                public int getHighFreq() {
                    return 0;
                }

                @Override
                public int getHintToneState() {
                    return 0;
                }

                @Override
                public int getLoudnessState() {
                    return 0;
                }

                @Override
                public int getLowFreq() {
                    return 0;
                }

                @Override
                public int getMaxBalanceFaderLevel() {
                    return 0;
                }

                @Override
                public int getMaxBandIndex() {
                    return 0;
                }

                @Override
                public int getMaxBandValue() {
                    return 0;
                }

                @Override
                public int getMidFreq() {
                    return 0;
                }

                @Override
                public int getMixPolicy() {
                    return 0;
                }

                @Override
                public int getPhoneMixPolicy() {
                    return 0;
                }

                @Override
                public int getReversePolicy() {
                    return 0;
                }

                @Override
                public int getScreenSoundState() {
                    return 0;
                }

                @Override
                public int getSoundFeature() {
                    return 0;
                }

                @Override
                public int getSoundField() {
                    return 0;
                }

                @Override
                public int getSpeedCoefMode() {
                    return 0;
                }

                @Override
                public int getSubwooferMode() {
                    return 0;
                }

                @Override
                public int getVehicleState() {
                    return 0;
                }

                @Override
                public boolean hasAmpVehicel() {
                    return false;
                }

                @Override
                public boolean isAtmos() {
                    return false;
                }

                @Override
                public boolean isBabyInCar() {
                    return false;
                }

                @Override
                public boolean isDtsEnabled() {
                    return false;
                }

                @Override
                public boolean isMasterMute() {
                    return masterMuted();
                }

                @Override
                public boolean isMicMute() {
                    return false;
                }

                @Override
                public boolean isNaviMix() {
                    return false;
                }

                @Override
                public boolean isStreamMute(int stream) {
                    validateStream(stream);

                    // OEM Engine master software mute covers navigation (1) and media (3).
                    // BT (4) shares the emulator media mixer; OEM IDs are not Android stream IDs.
                    return (stream == 1 || stream == 3 || stream == 4) && masterMuted();
                }

                @Override
                public void playSound(int value) {}

                @Override
                public void playTone() {}

                @Override
                public int setArkarmysMode(int value) {
                    return 0;
                }

                @Override
                public int setAtmos(boolean flag) {
                    return 0;
                }

                @Override
                public int setAtmosMode(int value) {
                    return 0;
                }

                @Override
                public int setBTSourceVolume(int volume) {
                    writeBtSourceVolume(volume);

                    return 0;
                }

                @Override
                public int setBabyInCar(boolean flag) {
                    return 0;
                }

                @Override
                public int setBalanceFaderLevel(int value, int value2) {
                    return 0;
                }

                @Override
                public int setBandValue(int value, int value2) {
                    return 0;
                }

                @Override
                public int setBassFreq(int value) {
                    return 0;
                }

                @Override
                public int setCanDspEffect(int value, int value2, int value3) {
                    return 0;
                }

                @Override
                public int setCanDspLoudness(boolean flag) {
                    return 0;
                }

                @Override
                public int setChannel(int value) {
                    return 0;
                }

                @Override
                public int setChimeMode(int value) {
                    return 0;
                }

                @Override
                public int setDtsEnabled(boolean flag) {
                    return 0;
                }

                @Override
                public int setDtsMode(int value) {
                    return 0;
                }

                @Override
                public int setEQMode(int value) {
                    return 0;
                }

                @Override
                public void setEcnr(boolean flag) {}

                @Override
                public int setHighFreq(int value) {
                    return 0;
                }

                @Override
                public int setHintToneState(int value) {
                    return 0;
                }

                @Override
                public int setLoudness(int value) {
                    return 0;
                }

                @Override
                public int setLowFreq(int value) {
                    return 0;
                }

                @Override
                public void setMasterMute(boolean muted, int flags) {
                    writeMasterMute(muted, flags);
                }

                @Override
                public void setMediaPlaying(boolean flag, Bundle bundle) {}

                @Override
                public int setMicMute(boolean flag) {
                    return 0;
                }

                @Override
                public int setMidFreq(int value) {
                    return 0;
                }

                @Override
                public int setMixPolicy(int value) {
                    return 0;
                }

                @Override
                public void setNaviMix(boolean flag) {}

                @Override
                public int setPhoneMixPolicy(int value) {
                    return 0;
                }

                @Override
                public int setReversePolicy(int value) {
                    return 0;
                }

                @Override
                public int setScreenSoundState(int value) {
                    return 0;
                }

                @Override
                public int setSoundFeature(int value) {
                    return 0;
                }

                @Override
                public int setSoundField(int value) {
                    return 0;
                }

                @Override
                public int setSpeedCoefMode(int value) {
                    return 0;
                }

                @Override
                public int setSpeedCoefVolume(int value) {
                    return 0;
                }

                @Override
                public void setStreamMute(int value, boolean flag) {}

                @Override
                public void setStreamVolume(int stream, int volume, int flags, String pkg) {
                    writeVolume(stream, volume, flags);
                }

                @Override
                public int setSubwooferMode(int value) {
                    return 0;
                }

                @Override
                public void setVoiceMute(boolean flag) {}

                @Override
                public int setWakeUpMode(int value) {
                    return 0;
                }

                @Override
                public void setXCallState(boolean flag) {}

                @Override
                public int volumeFadeIn(int value, IAudioFadeCallback cb) {
                    return 1;
                }

                @Override
                public int volumeFadeOut(int value, IAudioFadeCallback cb) {
                    return 1;
                }
            };

    /** The phone source volume of setBTSourceVolume, apart from the media group of stream 4. */
    private static final int BT_SOURCE_KEY = 4;

    private static void validateStream(int stream) {
        // Matching AudioPolicyManager.ensureValidStreamType permits OEM streams0..24.
        if (stream < 0 || stream > 24) {
            throw new IllegalArgumentException("bad-stream");
        }
    }

    /**
     * The OEM Engine keeps one volume per stream group: media 3/4/21/23, phone 0/2/6/24 and 9/20/22
     * share one value each; the lowest stream of a group names its stored volume.
     */
    private static int volumeKey(int stream) {
        switch (stream) {
            case 2:
            case 6:
            case 24:
                return 0;
            case 4:
            case 21:
            case 23:
                return 3;
            case 20:
            case 22:
                return 9;
            default:
                return stream;
        }
    }

    private int activeStream() {
        synchronized (mLock) {
            AudioPolicyInfo info = mCurrInfo != null ? mCurrInfo : mLastMediaInfo;

            return info == null ? 3 : info.streamType;
        }
    }

    private boolean masterMuted() {
        synchronized (mLock) {
            return getSharedPreferences("oem_audio_volume", MODE_PRIVATE).getInt("master-mute", 0)
                    != 0;
        }
    }

    private void writeMasterMute(boolean muted, int flags) {
        synchronized (mVolumeWrites) {
            boolean before = masterMuted();

            if (before == muted) {
                return;
            }

            persistMasterMute(muted);

            try {
                applyPlatformMediaVolume(activeStream(), readVolume(activeStream()));
            } catch (RuntimeException error) {
                // checkstyle-allow IllegalCatch: Rollback mute on Android failure; rethrow.
                synchronized (mLock) {
                    getSharedPreferences("oem_audio_volume", MODE_PRIVATE)
                            .edit()
                            .putInt("master-mute", before ? 1 : 0)
                            .commit();
                }

                throw error;
            }
        }

        notifyMuteCallbacks(muted, flags);
    }

    private void persistMasterMute(boolean muted) {
        synchronized (mLock) {
            if (!getSharedPreferences("oem_audio_volume", MODE_PRIVATE)
                    .edit()
                    .putInt("master-mute", muted ? 1 : 0)
                    .commit()) {
                throw new IllegalStateException("mute-persistence-failed");
            }
        }
    }

    private void notifyMuteCallbacks(final boolean muted, final int flags) {
        final List<IAudioPolicyInfoCallback> callbacks;

        synchronized (mInfoCallbacks) {
            callbacks = new ArrayList<>(mInfoCallbacks.values());
        }

        for (final IAudioPolicyInfoCallback cb : callbacks) {
            mHandler.postDelayed(
                    new Runnable() {
                        @Override
                        public void run() {
                            synchronized (mInfoCallbacks) {
                                if (mInfoCallbacks.get(cb.asBinder()) != cb) {
                                    return;
                                }
                            }

                            try {
                                // OEM Engine software master mute: navigation and media groups.
                                cb.onMuteStateChange(muted, flags);
                                cb.onStreamMuteStateChange(1, muted);
                                cb.onStreamMuteStateChange(3, muted);
                                cb.onStreamMuteStateChange(4, muted);
                            } catch (RemoteException error) {
                                synchronized (mInfoCallbacks) {
                                    IBinder binder = cb.asBinder();

                                    if (mInfoCallbacks.get(binder) != cb) {
                                        return;
                                    }

                                    mInfoCallbacks.remove(binder);
                                    IBinder.DeathRecipient death = mInfoDeaths.remove(binder);

                                    if (death != null) {
                                        binder.unlinkToDeath(death, 0);
                                    }
                                }
                            }
                        }
                    },
                    0);
        }
    }

    private int readVolume(int stream) {
        validateStream(stream);

        synchronized (mLock) {
            return storedVolume(volumeKey(stream));
        }
    }

    private int storedVolume(int stream) {
        int volume =
                getSharedPreferences("oem_audio_volume", MODE_PRIVATE)
                        .getInt("stream-" + stream, 0);

        if (volume < 0 || volume > 30) {
            throw new IllegalStateException("invalid-stored-volume");
        }

        return volume;
    }

    private void adjust(int stream, int direction, int step, int flags) {
        validateStream(stream);

        if (direction < -1 || direction > 1 || step < 0) {
            throw new IllegalArgumentException("bad-volume-adjustment");
        }

        final int volume;
        final boolean unmuted;

        synchronized (mVolumeWrites) {
            synchronized (mLock) {
                long wanted = storedVolume(volumeKey(stream)) + (long) direction * step;
                volume = (int) Math.max(0L, Math.min(30L, wanted));
            }

            unmuted = commitVolume(stream, volume, flags);
        }

        if (unmuted) {
            notifyMuteCallbacks(false, flags);
        }

        // The OEM engine translates request flags into a display-targeted UI callback.
        notifyVolumeCallbacks(stream, volume, volumeCallbackFlags(flags));
    }

    private void writeVolume(int stream, int requested, int flags) {
        validateStream(stream);
        int volume = Math.max(0, Math.min(30, requested));
        final boolean unmuted;

        synchronized (mVolumeWrites) {
            unmuted = commitVolume(stream, volume, flags);
        }

        if (unmuted) {
            notifyMuteCallbacks(false, flags);
        }

        notifyVolumeCallbacks(stream, volume, volumeCallbackFlags(flags));
    }

    private void writeBtSourceVolume(int requested) {
        int volume = Math.max(0, Math.min(30, requested));

        synchronized (mVolumeWrites) {
            synchronized (mLock) {
                persistVolume(BT_SOURCE_KEY, volume);
            }
        }

        // OEM Engine.setBTSourceVolume reports media stream 3 with the 256 BT source flag.
        notifyVolumeCallbacks(3, volume, 256);
    }

    /**
     * Called under mVolumeWrites; never hold mLock during an Android Binder call. As the OEM
     * Engine, a nonzero media or navigation volume ends the master mute unless the request carries
     * the 512 keep-mute flag; returns whether it did.
     */
    private boolean commitVolume(int stream, int volume, int flags) {
        int before = readVolume(stream);
        int key = volumeKey(stream);
        // Every OEM media stream is the media group, which plays through Android MUSIC.
        boolean media = key == 3;
        boolean unmute =
                (key == 3 || key == 1) && volume > 0 && (flags & 512) == 0 && masterMuted();

        try {
            if (unmute) {
                persistMasterMute(false);
            }

            if (media) {
                applyPlatformMediaVolume(stream, volume);
            }

            synchronized (mLock) {
                persistVolume(key, volume);
            }
        } catch (RuntimeException error) {
            // checkstyle-allow IllegalCatch: Rollback failed Android volume transaction.
            try {
                synchronized (mLock) {
                    persistVolume(key, before);
                }

                if (unmute) {
                    persistMasterMute(true);
                }
            } catch (RuntimeException rollback) {
                // checkstyle-allow IllegalCatch: Keep primary failure if prefs rollback fails.
                error.addSuppressed(rollback);
            }

            if (media) {
                try {
                    applyPlatformMediaVolume(stream, before);
                } catch (RuntimeException rollback) {
                    // checkstyle-allow IllegalCatch: Keep primary failure if audio rollback fails.
                    error.addSuppressed(rollback);
                }
            }

            throw error;
        }

        return unmute;
    }

    private void applyPlatformMediaVolume(int stream, int volume) {
        // This A9 profile: OEM USB/media, BT and navigation usage12 share Android MUSIC.
        // CarVersionUranus is false; API28 usage12 -> stream3 was guest-probed.
        // Other OEM stream numbers are not Android AudioManager stream IDs.
        if (volumeKey(stream) != 3) {
            return;
        }

        AudioManager mixer = (AudioManager) getSystemService(AUDIO_SERVICE);

        if (mixer == null) {
            throw new IllegalStateException("audio-mixer-unavailable");
        }

        int maximum = mixer.getStreamMaxVolume(AudioManager.STREAM_MUSIC);

        if (maximum <= 0) {
            throw new IllegalStateException("audio-mixer-invalid-range");
        }

        int target = masterMuted() ? 0 : (int) Math.round((double) volume * maximum / 30d);
        // The OEM callback owns its overlay; do not ask stock SystemUI to show one.
        mixer.setStreamVolume(AudioManager.STREAM_MUSIC, target, 0);

        if (mixer.getStreamVolume(AudioManager.STREAM_MUSIC) != target) {
            throw new IllegalStateException("audio-mixer-readback-mismatch");
        }
    }

    private static int volumeCallbackFlags(int requestFlags) {
        // A9 AudioPolicy Engine: SHOW_UI targets the left display unless RIGHT is explicit;
        // KeyManager's request-only 1024 bit is not part of the callback contract.
        int callbackFlags = requestFlags & (256 | 2);

        if ((requestFlags & 1) != 0) {
            callbackFlags |= 1 | ((requestFlags & 32) != 0 ? 32 : 16);
        }

        return callbackFlags;
    }

    private void persistVolume(int stream, int volume) {
        if (!getSharedPreferences("oem_audio_volume", MODE_PRIVATE)
                .edit()
                .putInt("stream-" + stream, volume)
                .commit()) {
            throw new IllegalStateException("volume-save-failed");
        }
    }

    private void notifyVolumeCallbacks(final int stream, final int volume, final int flags) {
        final List<IAudioPolicyInfoCallback> callbacks;

        synchronized (mInfoCallbacks) {
            callbacks = new ArrayList<>(mInfoCallbacks.values());
        }

        for (final IAudioPolicyInfoCallback cb : callbacks) {
            mHandler.postDelayed(
                    new Runnable() {
                        @Override
                        public void run() {
                            synchronized (mInfoCallbacks) {
                                if (mInfoCallbacks.get(cb.asBinder()) != cb) {
                                    return;
                                }
                            }

                            try {
                                cb.onStreamVolumeChange(stream, volume, flags);
                            } catch (RemoteException error) {
                                synchronized (mInfoCallbacks) {
                                    IBinder binder = cb.asBinder();

                                    if (mInfoCallbacks.get(binder) != cb) {
                                        return;
                                    }

                                    mInfoCallbacks.remove(binder);
                                    IBinder.DeathRecipient death = mInfoDeaths.remove(binder);

                                    if (death != null) {
                                        binder.unlinkToDeath(death, 0);
                                    }
                                }
                            }
                        }
                    },
                    0);
        }

        android.util.Log.i(
                TAG, "stream volume stream=" + stream + " volume=" + volume + " flags=" + flags);
    }

    private static AudioPolicyInfo copy(AudioPolicyInfo src) {
        if (src == null) {
            return null;
        }

        AudioPolicyInfo dst = new AudioPolicyInfo();
        dst.packageName = src.packageName;
        dst.streamType = src.streamType;
        dst.clientId = src.clientId;

        return dst;
    }

    private void notifyInfoCallbacks(AudioPolicyInfo info) {
        final List<IAudioPolicyInfoCallback> callbacks;

        synchronized (mInfoCallbacks) {
            callbacks = new ArrayList<>(mInfoCallbacks.values());
        }

        for (final IAudioPolicyInfoCallback cb : callbacks) {
            synchronized (mInfoCallbacks) {
                if (mInfoCallbacks.get(cb.asBinder()) != cb) {
                    continue;
                }
            }

            try {
                cb.onAudioPolicyInfoChange(copy(info));
            } catch (RemoteException e) {
                android.util.Log.w(TAG, "info callback dead: " + e);
            }
        }
    }

    @Override
    public IBinder onBind(Intent intent) {
        ensureStartupAudio();
        android.util.Log.i(TAG, "onBind " + intent);

        return mBinder;
    }

    @Override
    public boolean onUnbind(Intent intent) {
        android.util.Log.i(TAG, "onUnbind " + intent);

        return super.onUnbind(intent);
    }

    private void ensureStartupAudio() {
        synchronized (mVolumeWrites) {
            StartupAudioPolicy.zero(this);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        ensureStartupAudio();

        return START_NOT_STICKY;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        ensureStartupAudio();
        android.util.Log.i(TAG, "AudioPolicyStubService created");
    }
}
