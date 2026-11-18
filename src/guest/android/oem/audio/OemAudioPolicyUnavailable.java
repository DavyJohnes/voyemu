package com.voyemu.oem.audio;

/** Exact original unavailable branches; no hardware success or calibration is synthesized. */
final class OemAudioPolicyUnavailable {
    private static int diagnostics;

    static boolean matches(String signature) {
        switch (signature) {
            case "getArkarmysMode()I":
            case "getAtmosMode()I":
            case "getBTSourceVolume()I":
            case "getBalanceFaderLevel()[I":
            case "getBandValue(I)I":
            case "getBassFreq()I":
            case "getCanDspType()I":
            case "getChannel()I":
            case "getChimeMode()I":
            case "getDtsMode()I":
            case "getEQDefault()[I":
            case "getEQMode()Lcom/qinggan/audiopolicy/AudioEQMode;":
            case "getEQModeByIndex(I)Lcom/qinggan/audiopolicy/AudioEQMode;":
            case "getEQModeIndex()I":
            case "getHighFreq()I":
            case "getHintToneState()I":
            case "getLoudnessState()I":
            case "getLowFreq()I":
            case "getMaxBalanceFaderLevel()I":
            case "getMaxBandIndex()I":
            case "getMaxBandValue()I":
            case "getMidFreq()I":
            case "getMixPolicy()I":
            case "getPhoneMixPolicy()I":
            case "getReversePolicy()I":
            case "getScreenSoundState()I":
            case "getSoundFeature()I":
            case "getSoundField()I":
            case "getSpeedCoefMode()I":
            case "getSubwooferMode()I":
            case "getVehicleState()I":
            case "hasAmpVehicel()Z":
            case "isAtmos()Z":
            case "isBTSourceConnected()Z":
            case "isBabyInCar()Z":
            case "isDtsEnabled()Z":
            case "isInCall(Ljava/lang/String;)Z":
            case "isInIVoka()Z":
            case "playSound(I)V":
            case "playTone()V":
            case "registerAudioExceptionCallback(Lcom/qinggan/audiopolicy/IAudioExceptionCallback;)Z":
            case "registerBTSourceConnected(Lcom/qinggan/audiopolicy/IBTSourceConnected;Ljava/lang/String;)Z":
            case "registerBTSourceInfoCallback(Lcom/qinggan/audiopolicy/IBTSourceInfoCallback;Ljava/lang/String;)Z":
            case "registerSoundChannelForPkg(Ljava/lang/String;I)V":
            case "setArkarmysMode(I)I":
            case "setAtmos(Z)I":
            case "setAtmosMode(I)I":
            case "setBTSourceVolume(I)I":
            case "setBabyInCar(Z)I":
            case "setBalanceFaderLevel(II)I":
            case "setBandValue(II)I":
            case "setBassFreq(I)I":
            case "setCanDspEffect(III)I":
            case "setCanDspLoudness(Z)I":
            case "setChannel(I)I":
            case "setChimeMode(I)I":
            case "setDtsEnabled(Z)I":
            case "setDtsMode(I)I":
            case "setEQMode(I)I":
            case "setEQModeByIndex(I[I)V":
            case "setEcnr(Z)V":
            case "setHighFreq(I)I":
            case "setHintToneState(I)I":
            case "setLoudness(I)I":
            case "setLowFreq(I)I":
            case "setMidFreq(I)I":
            case "setMixPolicy(I)I":
            case "setPhoneMixPolicy(I)I":
            case "setReversePolicy(I)I":
            case "setScreenSoundState(I)I":
            case "setSoundFeature(I)I":
            case "setSoundField(I)I":
            case "setSpeedCoefMode(I)I":
            case "setSpeedCoefVolume(I)I":
            case "setSubwooferMode(I)I":
            case "setVoiceMute(Z)V":
            case "setWakeUpMode(I)I":
            case "setXCallState(Z)V":
            case "unregisterAudioExceptionCallback(Lcom/qinggan/audiopolicy/IAudioExceptionCallback;)Z":
            case "unregisterBTSourceConnected(Lcom/qinggan/audiopolicy/IBTSourceConnected;)Z":
            case "unregisterBTSourceInfoCallback(Lcom/qinggan/audiopolicy/IBTSourceInfoCallback;)Z":
            case "unregisterSoundChannelForPkg(Ljava/lang/String;)V":
            case "volumeFadeIn(ILcom/qinggan/audiopolicy/IAudioFadeCallback;)I":
            case "volumeFadeOut(ILcom/qinggan/audiopolicy/IAudioFadeCallback;)I":
                return true;
            default:
                return false;
        }
    }

    static Object dispatch(String signature) {
        synchronized (OemAudioPolicyUnavailable.class) {
            if (diagnostics++ < 8) {
                System.err.println("OemAudioPolicy unsupported: " + signature);
            }
        }

        return unsupportedRead(signature);
    }

    private static Object unsupportedRead(String signature) {
        switch (signature) {
            case "getArkarmysMode()I":
                return Integer.valueOf(-1);
            case "getAtmosMode()I":
                return Integer.valueOf(-1);
            case "getBTSourceVolume()I":
                return Integer.valueOf(-1);
            case "getBalanceFaderLevel()[I":
                return new int[0];
            case "getBandValue(I)I":
                return Integer.valueOf(-1);
            case "getBassFreq()I":
                return Integer.valueOf(-1);
            case "getCanDspType()I":
                return Integer.valueOf(-1);
            case "getChannel()I":
                return Integer.valueOf(-1);
            case "getChimeMode()I":
                return Integer.valueOf(-1);
            case "getDtsMode()I":
                return Integer.valueOf(-1);
            case "getEQDefault()[I":
                return null;
            case "getEQMode()Lcom/qinggan/audiopolicy/AudioEQMode;":
                return null;
            case "getEQModeByIndex(I)Lcom/qinggan/audiopolicy/AudioEQMode;":
                return null;
            case "getEQModeIndex()I":
                return Integer.valueOf(-1);
            case "getHighFreq()I":
                return Integer.valueOf(-1);
            case "getHintToneState()I":
                return Integer.valueOf(-1);
            case "getLoudnessState()I":
                return Integer.valueOf(-1);
            case "getLowFreq()I":
                return Integer.valueOf(-1);
            case "getMaxBalanceFaderLevel()I":
                return Integer.valueOf(-1);
            case "getMaxBandIndex()I":
                return Integer.valueOf(-1);
            case "getMaxBandValue()I":
                return Integer.valueOf(-1);
            case "getMidFreq()I":
                return Integer.valueOf(-1);
            case "getMixPolicy()I":
                return Integer.valueOf(-1);
            case "getPhoneMixPolicy()I":
                return Integer.valueOf(-1);
            case "getReversePolicy()I":
                return Integer.valueOf(-1);
            case "getScreenSoundState()I":
                return Integer.valueOf(-1);
            case "getSoundFeature()I":
                return Integer.valueOf(-1);
            case "getSoundField()I":
                return Integer.valueOf(-1);
            case "getSpeedCoefMode()I":
                return Integer.valueOf(-1);
            case "getSubwooferMode()I":
                return Integer.valueOf(-1);
            case "getVehicleState()I":
                return Integer.valueOf(-1);
            case "hasAmpVehicel()Z":
                return false;
            case "isAtmos()Z":
                return false;
            case "isBTSourceConnected()Z":
                return false;
            case "isBabyInCar()Z":
                return false;
            case "isDtsEnabled()Z":
                return false;
            case "isInCall(Ljava/lang/String;)Z":
                return false;
            case "isInIVoka()Z":
                return false;
            default:
                return unsupportedChange(signature);
        }
    }

    private static Object unsupportedChange(String signature) {
        switch (signature) {
            case "playSound(I)V":
                throw new UnsupportedOperationException(
                        "OemAudioPolicy unsupported hardware: playSound(I)V");
            case "playTone()V":
                throw new UnsupportedOperationException(
                        "OemAudioPolicy unsupported hardware: playTone()V");
            case "registerAudioExceptionCallback(Lcom/qinggan/audiopolicy/IAudioExceptionCallback;)Z":
                return false;
            case "registerBTSourceConnected(Lcom/qinggan/audiopolicy/IBTSourceConnected;Ljava/lang/String;)Z":
                return false;
            case "registerBTSourceInfoCallback(Lcom/qinggan/audiopolicy/IBTSourceInfoCallback;Ljava/lang/String;)Z":
                return false;
            case "registerSoundChannelForPkg(Ljava/lang/String;I)V":
                throw new UnsupportedOperationException(
                        "OemAudioPolicy unsupported hardware:"
                                + " registerSoundChannelForPkg(Ljava/lang/String;I)V");
            case "setArkarmysMode(I)I":
                return Integer.valueOf(-1);
            case "setAtmos(Z)I":
                return Integer.valueOf(-1);
            case "setAtmosMode(I)I":
                return Integer.valueOf(-1);
            case "setBTSourceVolume(I)I":
                return Integer.valueOf(-1);
            case "setBabyInCar(Z)I":
                throw new UnsupportedOperationException(
                        "OemAudioPolicy unsupported hardware: setBabyInCar(Z)I");
            case "setBalanceFaderLevel(II)I":
                return Integer.valueOf(-1);
            case "setBandValue(II)I":
                return Integer.valueOf(-1);
            case "setBassFreq(I)I":
                return Integer.valueOf(-1);
            case "setCanDspEffect(III)I":
                return Integer.valueOf(-1);
            case "setCanDspLoudness(Z)I":
                return Integer.valueOf(-1);
            case "setChannel(I)I":
                return Integer.valueOf(-1);
            case "setChimeMode(I)I":
                return Integer.valueOf(-1);
            case "setDtsEnabled(Z)I":
                return Integer.valueOf(-1);
            case "setDtsMode(I)I":
                return Integer.valueOf(-1);
            case "setEQMode(I)I":
                return Integer.valueOf(-1);
            case "setEQModeByIndex(I[I)V":
                throw new UnsupportedOperationException(
                        "OemAudioPolicy unsupported hardware: setEQModeByIndex(I[I)V");
            case "setEcnr(Z)V":
                throw new UnsupportedOperationException(
                        "OemAudioPolicy unsupported hardware: setEcnr(Z)V");
            case "setHighFreq(I)I":
                return Integer.valueOf(-1);
            case "setHintToneState(I)I":
                return Integer.valueOf(-1);
            case "setLoudness(I)I":
                return Integer.valueOf(-1);
            case "setLowFreq(I)I":
                return Integer.valueOf(-1);
            case "setMidFreq(I)I":
                return Integer.valueOf(-1);
            case "setMixPolicy(I)I":
                return Integer.valueOf(-1);
            case "setPhoneMixPolicy(I)I":
                return Integer.valueOf(-1);
            case "setReversePolicy(I)I":
                return Integer.valueOf(-1);
            case "setScreenSoundState(I)I":
                return Integer.valueOf(-1);
            case "setSoundFeature(I)I":
                return Integer.valueOf(-1);
            case "setSoundField(I)I":
                return Integer.valueOf(-1);
            case "setSpeedCoefMode(I)I":
                throw new UnsupportedOperationException(
                        "OemAudioPolicy unsupported hardware: setSpeedCoefMode(I)I");
            case "setSpeedCoefVolume(I)I":
                return Integer.valueOf(-1);
            case "setSubwooferMode(I)I":
                return Integer.valueOf(-1);
            case "setVoiceMute(Z)V":
                throw new UnsupportedOperationException(
                        "OemAudioPolicy unsupported hardware: setVoiceMute(Z)V");
            case "setWakeUpMode(I)I":
                return Integer.valueOf(-1);
            case "setXCallState(Z)V":
                throw new UnsupportedOperationException(
                        "OemAudioPolicy unsupported hardware: setXCallState(Z)V");
            case "unregisterAudioExceptionCallback(Lcom/qinggan/audiopolicy/IAudioExceptionCallback;)Z":
                return false;
            case "unregisterBTSourceConnected(Lcom/qinggan/audiopolicy/IBTSourceConnected;)Z":
                return false;
            case "unregisterBTSourceInfoCallback(Lcom/qinggan/audiopolicy/IBTSourceInfoCallback;)Z":
                return false;
            case "unregisterSoundChannelForPkg(Ljava/lang/String;)V":
                throw new UnsupportedOperationException(
                        "OemAudioPolicy unsupported hardware:"
                                + " unregisterSoundChannelForPkg(Ljava/lang/String;)V");
            case "volumeFadeIn(ILcom/qinggan/audiopolicy/IAudioFadeCallback;)I":
                return Integer.valueOf(-1);
            case "volumeFadeOut(ILcom/qinggan/audiopolicy/IAudioFadeCallback;)I":
                return Integer.valueOf(-1);
            default:
                throw new UnsupportedOperationException(
                        "OemAudioPolicy unknown contract: " + signature);
        }
    }
}
