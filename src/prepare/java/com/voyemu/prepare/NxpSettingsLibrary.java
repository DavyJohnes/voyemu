package com.voyemu.prepare;

import java.util.Set;

/**
 * Exact owner-supplied NXP Setting donor ABI (finite SystemUI-absent closure); generated donor
 * bytes remain outside source.
 */
final class NxpSettingsLibrary {
    static final String PIN = "b3a79c9cb737b5c84256c98b9e00553467bc9fda43b975398a7284a0a9cac741";

    /**
     * Setting classes.dex donor inventory: the 25 SystemUI-absent classes the OEM runtime family
     * needs (producers first).
     */
    static final Set<String> RUNTIME_TYPES =
            Set.of(
                    "Lcom/qinggan/account/AccountInfoBean;",
                    "Lcom/qinggan/account/AccountUserManager;",
                    "Lcom/qinggan/account/QGSupportMutiUserCommonhelper;",
                    "Lcom/qinggan/common/OnInitListener;",
                    "Lcom/qinggan/os/ExternalDevicesState;",
                    "Lcom/qinggan/os/FileUtils;",
                    "Lcom/qinggan/os/FileUtils$FileStatus;",
                    "Lcom/qinggan/os/NvramNative;",
                    "Lcom/qinggan/os/Runtime;",
                    "Lcom/qinggan/os/QGStorageVolume;",
                    "Lcom/qinggan/os/QGStorageVolume$1;",
                    "Lcom/qinggan/os/ServiceManager;",
                    "Lcom/qinggan/os/SystemProperties;",
                    "Lcom/qinggan/os/SystemProperties$FakeNvram;",
                    "Lcom/qinggan/provider/QGSettings;",
                    "Lcom/qinggan/provider/QGSettings$1;",
                    "Lcom/qinggan/provider/QGSettings$Global;",
                    "Lcom/qinggan/provider/QGSettings$Global$1;",
                    "Lcom/qinggan/provider/QGSettings$NameValueCache;",
                    "Lcom/qinggan/provider/QGSettings$NameValueTable;",
                    "Lcom/qinggan/provider/QGSettings$Secure;",
                    "Lcom/qinggan/provider/QGSettings$Secure$1;",
                    "Lcom/qinggan/provider/QGSettings$System;",
                    "Lcom/qinggan/provider/QGSettings$System$1;",
                    "Lcom/qinggan/provider/QGSettingsListener;");

    /**
     * Setting classes2.dex donor inventory: the 50 SystemUI-absent manager classes
     * (com.qinggan.util.Log is owned by the media-callbacks donor).
     */
    static final Set<String> MANAGER_TYPES =
            Set.of(
                    "Lcom/qinggan/system/BackLightManager;",
                    "Lcom/qinggan/system/BackLightManager$1;",
                    "Lcom/qinggan/system/BackLightManager$2;",
                    "Lcom/qinggan/system/BackLightManager$3;",
                    "Lcom/qinggan/system/BackLightManager$4;",
                    "Lcom/qinggan/system/BackLightManager$OnBackLightListener;",
                    "Lcom/qinggan/system/BackLightManager$OnScreenBackLightListener;",
                    "Lcom/qinggan/system/BackLightManager$ScreenBackLightCallback;",
                    "Lcom/qinggan/system/BackLightManager$ScreenBackLightCallback$1;",
                    "Lcom/qinggan/system/IBTAutoConnectCallback;",
                    "Lcom/qinggan/system/IBTAutoConnectCallback$Stub;",
                    "Lcom/qinggan/system/IBTAutoConnectCallback$Stub$Proxy;",
                    "Lcom/qinggan/system/IBackLightCallback;",
                    "Lcom/qinggan/system/IBackLightCallback$Stub;",
                    "Lcom/qinggan/system/IBackLightCallback$Stub$Proxy;",
                    "Lcom/qinggan/system/IMultiWindowService;",
                    "Lcom/qinggan/system/IMultiWindowService$Stub;",
                    "Lcom/qinggan/system/IMultiWindowService$Stub$Proxy;",
                    "Lcom/qinggan/system/INightVisionCallback;",
                    "Lcom/qinggan/system/INightVisionCallback$Stub;",
                    "Lcom/qinggan/system/INightVisionCallback$Stub$Proxy;",
                    "Lcom/qinggan/system/IReverseCallback;",
                    "Lcom/qinggan/system/IReverseCallback$Stub;",
                    "Lcom/qinggan/system/IReverseCallback$Stub$Proxy;",
                    "Lcom/qinggan/system/ISystemPolicy;",
                    "Lcom/qinggan/system/ISystemPolicy$Stub;",
                    "Lcom/qinggan/system/ISystemPolicy$Stub$Proxy;",
                    "Lcom/qinggan/system/MultiWindowManager;",
                    "Lcom/qinggan/system/MultiWindowManager$1;",
                    "Lcom/qinggan/system/MultiWindowManager$2;",
                    "Lcom/qinggan/system/MultiWindowManager$CityObserver;",
                    "Lcom/qinggan/system/MultiWindowManager$Connection;",
                    "Lcom/qinggan/system/SystemPolicyManager;",
                    "Lcom/qinggan/system/SystemPolicyManager$1;",
                    "Lcom/qinggan/system/SystemPolicyManager$2;",
                    "Lcom/qinggan/system/SystemPolicyManager$3;",
                    "Lcom/qinggan/system/SystemPolicyManager$OnInitHandler;",
                    "Lcom/qinggan/tbox/ITBoxListener;",
                    "Lcom/qinggan/tbox/ITBoxListener$Stub;",
                    "Lcom/qinggan/tbox/ITBoxListener$Stub$Proxy;",
                    "Lcom/qinggan/tbox/ITBoxService;",
                    "Lcom/qinggan/tbox/ITBoxService$Stub;",
                    "Lcom/qinggan/tbox/ITBoxService$Stub$Proxy;",
                    "Lcom/qinggan/tbox/TBoxListener;",
                    "Lcom/qinggan/tbox/TBoxManager;",
                    "Lcom/qinggan/tbox/TBoxManager$1;",
                    "Lcom/qinggan/tbox/TBoxManager$2;",
                    "Lcom/qinggan/tbox/TBoxManager$ObtainServiceThread;",
                    "Lcom/qinggan/tbox/TBoxManager$OnInitListener;",
                    "Lcom/qinggan/tbox/TBoxManager$TBoxHandler;");

    static final Set<String> SYSTEM_UI_RUNTIME_A =
            Set.of(
                    "Lcom/qinggan/app/ActivityWatcher;",
                    "Lcom/qinggan/app/CrashHandler;",
                    "Lcom/qinggan/app/CrashInfo;",
                    "Lcom/qinggan/app/QGApplication;",
                    "Lcom/qinggan/audiopolicy/AudioEQMode$1;",
                    "Lcom/qinggan/audiopolicy/AudioEQMode;",
                    "Lcom/qinggan/audiopolicy/AudioPolicyInfo$1;",
                    "Lcom/qinggan/audiopolicy/AudioPolicyInfo;",
                    "Lcom/qinggan/audiopolicy/AudioPolicyManager$1;",
                    "Lcom/qinggan/audiopolicy/AudioPolicyManager$2;",
                    "Lcom/qinggan/audiopolicy/AudioPolicyManager$3;",
                    "Lcom/qinggan/audiopolicy/AudioPolicyManager$4;",
                    "Lcom/qinggan/audiopolicy/AudioPolicyManager$5;",
                    "Lcom/qinggan/audiopolicy/AudioPolicyManager$6;",
                    "Lcom/qinggan/audiopolicy/AudioPolicyManager$7;",
                    "Lcom/qinggan/audiopolicy/AudioPolicyManager$IAudioExceptionListener;",
                    "Lcom/qinggan/audiopolicy/AudioPolicyManager$IAudioFadeListener;",
                    "Lcom/qinggan/audiopolicy/AudioPolicyManager$IAudioPolicyInfoListener;",
                    "Lcom/qinggan/audiopolicy/AudioPolicyManager$OnAudioPolicyListener;",
                    "Lcom/qinggan/audiopolicy/AudioPolicyManager$OnAudioPolicyListenerEx;",
                    "Lcom/qinggan/audiopolicy/AudioPolicyManager$OnInitListener;",
                    "Lcom/qinggan/audiopolicy/AudioPolicyManager;",
                    "Lcom/qinggan/audiopolicy/IAudioExceptionCallback$Stub$Proxy;",
                    "Lcom/qinggan/audiopolicy/IAudioExceptionCallback$Stub;",
                    "Lcom/qinggan/audiopolicy/IAudioExceptionCallback;",
                    "Lcom/qinggan/audiopolicy/IAudioFadeCallback$Stub$Proxy;",
                    "Lcom/qinggan/audiopolicy/IAudioFadeCallback$Stub;",
                    "Lcom/qinggan/audiopolicy/IAudioFadeCallback;",
                    "Lcom/qinggan/audiopolicy/IAudioPolicyCallback$Stub$Proxy;",
                    "Lcom/qinggan/audiopolicy/IAudioPolicyCallback$Stub;",
                    "Lcom/qinggan/audiopolicy/IAudioPolicyCallback;",
                    "Lcom/qinggan/audiopolicy/IAudioPolicyInfoCallback$Stub$Proxy;",
                    "Lcom/qinggan/audiopolicy/IAudioPolicyInfoCallback$Stub;",
                    "Lcom/qinggan/audiopolicy/IAudioPolicyInfoCallback;",
                    "Lcom/qinggan/audiopolicy/IAudioPolicyService$Stub$Proxy;",
                    "Lcom/qinggan/audiopolicy/IAudioPolicyService$Stub;",
                    "Lcom/qinggan/audiopolicy/IAudioPolicyService;",
                    "Lcom/qinggan/bluetooth/BTAdapterFactory;",
                    "Lcom/qinggan/bluetooth/BTConst;",
                    "Lcom/qinggan/bluetooth/BTDeviceManager;",
                    "Lcom/qinggan/bluetooth/BTManager;",
                    "Lcom/qinggan/bluetooth/bean/BTCallLogItem;",
                    "Lcom/qinggan/bluetooth/bean/BTContactItem;",
                    "Lcom/qinggan/bluetooth/bean/BTMediaInfo$1;",
                    "Lcom/qinggan/bluetooth/bean/BTMediaInfo;",
                    "Lcom/qinggan/bluetooth/bean/BTPhoneMessage;",
                    "Lcom/qinggan/bluetooth/internal/util/Log;",
                    "Lcom/qinggan/bluetooth/listener/BTStateListener;",
                    "Lcom/qinggan/bluetooth/profile/A2dp$1;",
                    "Lcom/qinggan/bluetooth/profile/A2dp;",
                    "Lcom/qinggan/bluetooth/profile/BTAdapter$1;",
                    "Lcom/qinggan/bluetooth/profile/BTAdapter;",
                    "Lcom/qinggan/bluetooth/profile/BTDevice;",
                    "Lcom/qinggan/bluetooth/profile/Hfp$1;",
                    "Lcom/qinggan/bluetooth/profile/Hfp;",
                    "Lcom/qinggan/bluetooth/profile/Hid;",
                    "Lcom/qinggan/bluetooth/profile/IA2dpCallback;",
                    "Lcom/qinggan/bluetooth/profile/IBTCallback;",
                    "Lcom/qinggan/bluetooth/profile/IBTProfile;",
                    "Lcom/qinggan/bluetooth/profile/IBTProfileCallback;",
                    "Lcom/qinggan/bluetooth/profile/IHfpCallback;",
                    "Lcom/qinggan/bluetooth/profile/IHidCallback;",
                    "Lcom/qinggan/bluetooth/profile/IPbapCallback;",
                    "Lcom/qinggan/bluetooth/profile/IRfcommCallback;");
    static final Set<String> SYSTEM_UI_RUNTIME_B =
            Set.of(
                    "Lcom/qinggan/bluetooth/profile/ISppCallback;",
                    "Lcom/qinggan/bluetooth/profile/Pbap$1;",
                    "Lcom/qinggan/bluetooth/profile/Pbap;",
                    "Lcom/qinggan/bluetooth/profile/Rfcomm$1;",
                    "Lcom/qinggan/bluetooth/profile/Rfcomm;",
                    "Lcom/qinggan/bluetooth/profile/SimpleHfpCallback;",
                    "Lcom/qinggan/bluetooth/profile/SimplePbapCallback;",
                    "Lcom/qinggan/bluetooth/profile/Spp$1;",
                    "Lcom/qinggan/bluetooth/profile/Spp;",
                    "Lcom/qinggan/common/QGConfig;",
                    "Lcom/qinggan/common/QGManagerBase$1;",
                    "Lcom/qinggan/common/QGManagerBase$2;",
                    "Lcom/qinggan/common/QGManagerBase$3;",
                    "Lcom/qinggan/common/QGManagerBase;",
                    "Lcom/qinggan/multidisplay/ActivityAnimationController$ActivityAnimation$AbandonFrameCallback;",
                    "Lcom/qinggan/multidisplay/ActivityAnimationController$ActivityAnimation$FlingFrameCallback;",
                    "Lcom/qinggan/multidisplay/ActivityAnimationController$ActivityAnimation;",
                    "Lcom/qinggan/multidisplay/ActivityAnimationController;",
                    "Lcom/qinggan/multidisplay/AnimationController$1;",
                    "Lcom/qinggan/multidisplay/AnimationController$2;",
                    "Lcom/qinggan/multidisplay/AnimationController$MultiGestureListener;",
                    "Lcom/qinggan/multidisplay/AnimationController$StartAnimArgs;",
                    "Lcom/qinggan/multidisplay/AnimationController;",
                    "Lcom/qinggan/multidisplay/IMultiDisplayAnimationObserver$Stub$Proxy;",
                    "Lcom/qinggan/multidisplay/IMultiDisplayAnimationObserver$Stub;",
                    "Lcom/qinggan/multidisplay/IMultiDisplayAnimationObserver;",
                    "Lcom/qinggan/multidisplay/IMultiDisplayListener$Stub$Proxy;",
                    "Lcom/qinggan/multidisplay/IMultiDisplayListener$Stub;",
                    "Lcom/qinggan/multidisplay/IMultiDisplayListener;",
                    "Lcom/qinggan/multidisplay/IMultiDisplayMovingObserver$Stub$Proxy;",
                    "Lcom/qinggan/multidisplay/IMultiDisplayMovingObserver$Stub;",
                    "Lcom/qinggan/multidisplay/IMultiDisplayMovingObserver;",
                    "Lcom/qinggan/multidisplay/IMultiDisplayService$Stub$Proxy;",
                    "Lcom/qinggan/multidisplay/IMultiDisplayService$Stub;",
                    "Lcom/qinggan/multidisplay/IMultiDisplayService;",
                    "Lcom/qinggan/multidisplay/MultiDisplayAnimationObservable$1;",
                    "Lcom/qinggan/multidisplay/MultiDisplayAnimationObservable;",
                    "Lcom/qinggan/multidisplay/MultiDisplayManager$1;",
                    "Lcom/qinggan/multidisplay/MultiDisplayManager$2;",
                    "Lcom/qinggan/multidisplay/MultiDisplayManager$3;",
                    "Lcom/qinggan/multidisplay/MultiDisplayManager$AnimationSetting;",
                    "Lcom/qinggan/multidisplay/MultiDisplayManager$IMultiDisplayAnimListener;",
                    "Lcom/qinggan/multidisplay/MultiDisplayManager$MoveActivityRunnable;",
                    "Lcom/qinggan/multidisplay/MultiDisplayManager$MultiDisplayMovingObserver;",
                    "Lcom/qinggan/multidisplay/MultiDisplayManager$MultiDisplayObserver;",
                    "Lcom/qinggan/multidisplay/MultiDisplayManager$OnMovedToDisplayListener;",
                    "Lcom/qinggan/multidisplay/MultiDisplayManager;",
                    "Lcom/qinggan/multidisplay/MultiDisplayMovingObservable$1;",
                    "Lcom/qinggan/multidisplay/MultiDisplayMovingObservable;",
                    "Lcom/qinggan/multidisplay/MultiDisplayObservable;",
                    "Lcom/qinggan/multidisplay/MultiGestureDetector$MultiGestureCallback;",
                    "Lcom/qinggan/multidisplay/MultiGestureDetector;",
                    "Lcom/qinggan/multidisplay/WindowAnimationController$AnimationThread;",
                    "Lcom/qinggan/multidisplay/WindowAnimationController;",
                    "Lcom/qinggan/os/BackLightNative;",
                    "Lcom/qinggan/os/BundleFormat;",
                    "Lcom/qinggan/os/MiniRecoveryNative;",
                    "Lcom/qinggan/os/SystemInfo;",
                    "Lcom/qinggan/provider/SettingsConst$Global;",
                    "Lcom/qinggan/provider/SettingsConst$Secure;",
                    "Lcom/qinggan/provider/SettingsConst$System;",
                    "Lcom/qinggan/provider/SettingsConst;",
                    "Lcom/qinggan/sched/notification/NotificationConstants$NotificationLevel;");
    static final Set<String> SYSTEM_UI_RUNTIME_C =
            Set.of(
                    "Lcom/qinggan/system/AreaFocusModel$1;",
                    "Lcom/qinggan/system/AreaFocusModel;",
                    "Lcom/qinggan/system/IDirectionEventListener$Stub$Proxy;",
                    "Lcom/qinggan/system/IDirectionEventListener$Stub;",
                    "Lcom/qinggan/system/IDirectionEventListener;",
                    "Lcom/qinggan/system/IKeyManagerCallback$Stub$Proxy;",
                    "Lcom/qinggan/system/IKeyManagerCallback$Stub;",
                    "Lcom/qinggan/system/IKeyManagerCallback;",
                    "Lcom/qinggan/system/IKeyManagerService$Stub$Proxy;",
                    "Lcom/qinggan/system/IKeyManagerService$Stub;",
                    "Lcom/qinggan/system/IKeyManagerService;",
                    "Lcom/qinggan/system/KeyManager$1;",
                    "Lcom/qinggan/system/KeyManager$2;",
                    "Lcom/qinggan/system/KeyManager$3;",
                    "Lcom/qinggan/system/KeyManager$4;",
                    "Lcom/qinggan/system/KeyManager$5;",
                    "Lcom/qinggan/system/KeyManager$6;",
                    "Lcom/qinggan/system/KeyManager$7;",
                    "Lcom/qinggan/system/KeyManager$OnInitListener;",
                    "Lcom/qinggan/system/KeyManager$OnKeyEventListener;",
                    "Lcom/qinggan/system/KeyManager;",
                    "Lcom/qinggan/system/QGKeyEvent$1;",
                    "Lcom/qinggan/system/QGKeyEvent;",
                    "Lcom/qinggan/util/VehicleHelper;");

    static Set<String> mediaTypes() {
        var types = new java.util.HashSet<>(NxpMediaLibrary.TYPES);

        types.add("Lcom/qinggan/util/Log;");

        return Set.copyOf(types);
    }

    static boolean slot(String profile, int api, String reference, Set<String> types) {
        if (api != 28) {
            return false;
        }

        return switch (reference) {
            case "component:media-callbacks" -> mediaTypes().equals(types);
            case "component:settings-runtime" -> RUNTIME_TYPES.equals(types);
            case "component:settings-managers" -> MANAGER_TYPES.equals(types);
            case "component:systemui-runtime-a" -> SYSTEM_UI_RUNTIME_A.equals(types);
            case "component:systemui-runtime-b" -> SYSTEM_UI_RUNTIME_B.equals(types);
            case "component:systemui-runtime-c" -> SYSTEM_UI_RUNTIME_C.equals(types);
            default -> false;
        };
    }

    static boolean selected(int api, String pin, String output, Set<String> types) {
        if (!(api == 28 && PIN.equals(pin))) {
            return false;
        }

        if (output.equals("VoyemuNxpSettingsRuntime.jar")) {
            return RUNTIME_TYPES.equals(types);
        }

        if (output.equals("VoyemuNxpSettingsManagers.jar")) {
            return MANAGER_TYPES.equals(types);
        }

        if (output.equals("VoyemuNxpSystemUiRuntimeA.jar")) {
            return SYSTEM_UI_RUNTIME_A.equals(types);
        }

        if (output.equals("VoyemuNxpSystemUiRuntimeB.jar")) {
            return SYSTEM_UI_RUNTIME_B.equals(types);
        }

        if (output.equals("VoyemuNxpSystemUiRuntimeC.jar")) {
            return SYSTEM_UI_RUNTIME_C.equals(types);
        }

        return false;
    }
}
