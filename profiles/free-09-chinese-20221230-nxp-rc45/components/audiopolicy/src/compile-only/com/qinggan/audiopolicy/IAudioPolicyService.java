package com.qinggan.audiopolicy;

public interface IAudioPolicyService extends android.os.IInterface {
    abstract class Stub extends android.os.Binder implements IAudioPolicyService {
        public Stub() {}

        @Override
        public android.os.IBinder asBinder() {
            return null;
        }
    }

    int abandonAudioFocus(IAudioPolicyCallback p0, String p1, String p2, boolean p3)
            throws android.os.RemoteException;

    int abandonAudioPolicy(IAudioPolicyCallback p0, String p1, boolean p2)
            throws android.os.RemoteException;

    void adjustStreamVolume(int p0, int p1, int p2, int p3) throws android.os.RemoteException;

    void adjustVolume(int p0, int p1) throws android.os.RemoteException;

    int getArkarmysMode() throws android.os.RemoteException;

    int getAtmosMode() throws android.os.RemoteException;

    java.util.List<AudioPolicyInfo> getAudioPolicyInfo(int p0) throws android.os.RemoteException;

    int getBTSourceVolume() throws android.os.RemoteException;

    int[] getBalanceFaderLevel() throws android.os.RemoteException;

    int getBandValue(int p0) throws android.os.RemoteException;

    int getBassFreq() throws android.os.RemoteException;

    int getCanDspType() throws android.os.RemoteException;

    int getChannel() throws android.os.RemoteException;

    int getChimeMode() throws android.os.RemoteException;

    AudioPolicyInfo getCurrentAudioPolicyInfo(int p0) throws android.os.RemoteException;

    int getDtsMode() throws android.os.RemoteException;

    AudioEQMode getEQMode() throws android.os.RemoteException;

    AudioEQMode getEQModeByIndex(int p0) throws android.os.RemoteException;

    int getEQModeIndex() throws android.os.RemoteException;

    int getHighFreq() throws android.os.RemoteException;

    int getHintToneState() throws android.os.RemoteException;

    int getLoudnessState() throws android.os.RemoteException;

    int getLowFreq() throws android.os.RemoteException;

    int getMaxBalanceFaderLevel() throws android.os.RemoteException;

    int getMaxBandIndex() throws android.os.RemoteException;

    int getMaxBandValue() throws android.os.RemoteException;

    int getMidFreq() throws android.os.RemoteException;

    int getMixPolicy() throws android.os.RemoteException;

    int getPhoneMixPolicy() throws android.os.RemoteException;

    int getReversePolicy() throws android.os.RemoteException;

    int getScreenSoundState() throws android.os.RemoteException;

    int getSoundFeature() throws android.os.RemoteException;

    int getSoundField() throws android.os.RemoteException;

    int getSpeedCoefMode() throws android.os.RemoteException;

    int getStreamMaxVolume(int p0) throws android.os.RemoteException;

    int getStreamVolume(int p0) throws android.os.RemoteException;

    int getSubwooferMode() throws android.os.RemoteException;

    int getVehicleState() throws android.os.RemoteException;

    boolean hasAmpVehicel() throws android.os.RemoteException;

    boolean isAtmos() throws android.os.RemoteException;

    boolean isBabyInCar() throws android.os.RemoteException;

    boolean isDtsEnabled() throws android.os.RemoteException;

    boolean isInCall() throws android.os.RemoteException;

    boolean isInIVoka() throws android.os.RemoteException;

    boolean isMasterMute() throws android.os.RemoteException;

    boolean isMicMute() throws android.os.RemoteException;

    boolean isNaviMix() throws android.os.RemoteException;

    boolean isStreamMute(int p0) throws android.os.RemoteException;

    void playSound(int p0) throws android.os.RemoteException;

    void playTone() throws android.os.RemoteException;

    boolean registerAudioExceptionCallback(IAudioExceptionCallback p0)
            throws android.os.RemoteException;

    boolean registerAudioPolicyInfoCallback(IAudioPolicyInfoCallback p0, String p1)
            throws android.os.RemoteException;

    void registerSoundChannelForPid(int p0, int p1) throws android.os.RemoteException;

    int requestAudioFocus(
            IAudioPolicyCallback p0,
            android.os.IBinder p1,
            String p2,
            int p3,
            int p4,
            String p5,
            int p6,
            int p7)
            throws android.os.RemoteException;

    int requestAudioPolicy(
            int p0,
            int p1,
            android.os.IBinder p2,
            IAudioPolicyCallback p3,
            String p4,
            String p5,
            int p6)
            throws android.os.RemoteException;

    int requestAudioPolicyEx(
            int p0,
            int p1,
            android.os.IBinder p2,
            IAudioPolicyCallback p3,
            String p4,
            String p5,
            int p6,
            int p7)
            throws android.os.RemoteException;

    int setArkarmysMode(int p0) throws android.os.RemoteException;

    int setAtmos(boolean p0) throws android.os.RemoteException;

    int setAtmosMode(int p0) throws android.os.RemoteException;

    int setBTSourceVolume(int p0) throws android.os.RemoteException;

    int setBabyInCar(boolean p0) throws android.os.RemoteException;

    int setBalanceFaderLevel(int p0, int p1) throws android.os.RemoteException;

    int setBandValue(int p0, int p1) throws android.os.RemoteException;

    int setBassFreq(int p0) throws android.os.RemoteException;

    int setCanDspEffect(int p0, int p1, int p2) throws android.os.RemoteException;

    int setCanDspLoudness(boolean p0) throws android.os.RemoteException;

    int setChannel(int p0) throws android.os.RemoteException;

    int setChimeMode(int p0) throws android.os.RemoteException;

    int setDtsEnabled(boolean p0) throws android.os.RemoteException;

    int setDtsMode(int p0) throws android.os.RemoteException;

    int setEQMode(int p0) throws android.os.RemoteException;

    int setHighFreq(int p0) throws android.os.RemoteException;

    int setHintToneState(int p0) throws android.os.RemoteException;

    int setLoudness(int p0) throws android.os.RemoteException;

    int setLowFreq(int p0) throws android.os.RemoteException;

    void setMasterMute(boolean p0, int p1) throws android.os.RemoteException;

    int setMicMute(boolean p0) throws android.os.RemoteException;

    int setMidFreq(int p0) throws android.os.RemoteException;

    int setMixPolicy(int p0) throws android.os.RemoteException;

    void setNaviMix(boolean p0) throws android.os.RemoteException;

    int setPhoneMixPolicy(int p0) throws android.os.RemoteException;

    int setReversePolicy(int p0) throws android.os.RemoteException;

    int setScreenSoundState(int p0) throws android.os.RemoteException;

    int setSoundFeature(int p0) throws android.os.RemoteException;

    int setSoundField(int p0) throws android.os.RemoteException;

    int setSpeedCoefMode(int p0) throws android.os.RemoteException;

    int setSpeedCoefVolume(int p0) throws android.os.RemoteException;

    void setStreamMute(int p0, boolean p1) throws android.os.RemoteException;

    void setStreamVolume(int p0, int p1, int p2) throws android.os.RemoteException;

    int setSubwooferMode(int p0) throws android.os.RemoteException;

    void setVoiceMute(boolean p0) throws android.os.RemoteException;

    int setWakeUpMode(int p0) throws android.os.RemoteException;

    boolean unregisterAudioExceptionCallback(IAudioExceptionCallback p0)
            throws android.os.RemoteException;

    void unregisterAudioPolicyClient(String p0) throws android.os.RemoteException;

    boolean unregisterAudioPolicyInfoCallback(IAudioPolicyInfoCallback p0)
            throws android.os.RemoteException;

    void unregisterSoundChannelForPid(int p0) throws android.os.RemoteException;

    int volumeFadeIn(int p0, IAudioFadeCallback p1) throws android.os.RemoteException;

    int volumeFadeOut(int p0, IAudioFadeCallback p1) throws android.os.RemoteException;
}
