package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Pattern;

/** Registered own handlers plus build-exported immutable data; no script runtime. */
final class Recipes {
    private static final Set<String> REGISTERED =
            Set.of(
                    "nxp-systempolicy-hardware.json",
                    "systempolicy-hardware.json",
                    "nxp-voice-arm64-natives.json",
                    "nxp-settings-voice-initialization.json",
                    "nxp-phone-connect-anr.json",
                    "nxp-phone-provider.json",
                    "can-early-policy-a9.json",
                    "can-early-file-contexts-a9.json",
                    "can-early-property-contexts-a9.json",
                    "can-simulator-policy-a9.json",
                    "can-simulator-contexts-a9.json",
                    "selinux-canbus-nxp.json",
                    "selinux-bounded-su-nxp.json",
                    "selinux-service-contexts-nxp.json",
                    "selinux-hash-nxp.json",
                    "phone-provider.json",
                    "nxp-systemui-display-routing.json",
                    "nxp-systemui-media-library.json",
                    "nxp-systemui-settings-managers.json",
                    "nxp-launcher-display-routing.json",
                    "add-qg-runtime.json",
                    "qgapi-use-libraries.json",
                    "aiqiyi-netutil-ethernet-guard.json",
                    "camera-capabilities.json",
                    "disable-broadcastradio-a9.json",
                    "disable-incidentd-a9.json",
                    "disable-mediametrics-a9.json",
                    "disable-perfprofd-a9.json",
                    "disable-rild-a9.json",
                    "disable-statsd-a9.json",
                    "shutdown-router-a9.json",
                    "ime-display-routing/ime-display-routing.json",
                    "nxp-music-media-compat.json",
                    "ivoka-display-tasks.json",
                    "ivoka-silent-foreground.json",
                    "launcher-display-routing/launcher-display-routing.json",
                    "media-card-music-fix.json",
                    "media-runtime.json",
                    "api28-usb-player.json",
                    "api28-gallery-arm64.json",
                    "music-arm64-libs.json",
                    "navi-consent/navi-consent.json",
                    "navi-gles-msaa.json",
                    "navi-silent-foreground.json",
                    "network-ap-host-stub.json",
                    "qgime-arm64-natives.json",
                    "selinux-bounded-su-a9.json",
                    "selinux-canbus-a9.json",
                    "selinux-hash.json",
                    "selinux-property-contexts-a9.json",
                    "selinux-service-contexts-a9.json",
                    "setting-display-tasks.json",
                    "setting-framework-gaps.json",
                    "settingnetwork-framework-gaps.json",
                    "systemservice-framework.json",
                    "systemservice-network.json",
                    "systemui-display-routing.json",
                    "usercenter-network/usercenter-network.json");

    private static final Set<String> EXPLICIT_HANDLERS =
            Set.of(
                    "nxp-voice-arm64-natives.json",
                    "nxp-phone-provider.json",
                    "nxp-phone-connect-anr.json",
                    "systempolicy-hardware.json",
                    "can-early-policy-a9.json",
                    "can-early-file-contexts-a9.json",
                    "can-early-property-contexts-a9.json",
                    "can-simulator-policy-a9.json",
                    "can-simulator-contexts-a9.json",
                    "selinux-canbus-nxp.json",
                    "selinux-bounded-su-nxp.json",
                    "selinux-service-contexts-nxp.json",
                    "selinux-hash-nxp.json",
                    "phone-provider.json",
                    "add-qg-runtime.json",
                    "qgime-arm64-natives.json",
                    "launcher-display-routing/launcher-display-routing.json",
                    "media-card-music-fix.json",
                    "usercenter-network/usercenter-network.json",
                    "selinux-hash.json",
                    "selinux-bounded-su-a9.json",
                    "selinux-canbus-a9.json",
                    "selinux-property-contexts-a9.json",
                    "selinux-service-contexts-a9.json",
                    "systemservice-framework.json",
                    "systemservice-network.json",
                    "systemui-display-routing.json",
                    "settingnetwork-framework-gaps.json",
                    "media-runtime.json",
                    "api28-usb-player.json",
                    "camera-capabilities.json");

    static boolean hasRegisteredCoverage(String name, Map<String, Object> metadata) {
        if (!REGISTERED.contains(name)) {
            return false;
        }

        if (EXPLICIT_HANDLERS.contains(name)) {
            return true;
        }

        return metadata.get("calls") instanceof List<?> calls && !calls.isEmpty();
    }

    interface Target {
        default String profile() {
            return "";
        }

        /** The constants of the step this target applies; guards read their values from them. */
        default Map<String, Object> constants() {
            return Map.of();
        }

        int api();

        String abi();

        Path work();

        default DexClasses dex() {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Method edits require API28 DEX", 5);
        }

        Path output();

        Map<String, Path> uses();

        void addDex(String source) throws Exception;

        void putEntry(Path source, String name) throws Exception;

        void removeEntry(String name) throws Exception;

        default void verifyPinnedEntry(String name, String sha256) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Pinned native entry adapter missing", 5);
        }

        default void removePinnedEntry(String name, String sha256) throws Exception {
            verifyPinnedEntry(name, sha256);
            removeEntry(name);
        }

        void useLibraries(List<String> names) throws Exception;

        default void addNxpPhoneProvider() throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "NXP phone provider adapter missing", 5);
        }

        default void addPhoneProvider() throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "A9 phone provider adapter missing", 5);
        }

        default void guardPhoneProvider(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Phone provider adapter missing", 5);
        }

        default void guardAudioPolicy(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Private AudioPolicy adapter missing", 5);
        }

        default void guardAudioConsumer(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED",
                    "Private AudioPolicy consumer adapter missing",
                    5);
        }

        default void guardMediaNotification(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Notification adapter missing", 5);
        }

        default void guardPassionMcuProperty(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED",
                    "Passion partial MCU property adapter missing",
                    5);
        }

        default void guardPassionNavigationReady(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED",
                    "Passion navigation readiness adapter missing",
                    5);
        }

        default void guardPassionStatusReady(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Passion status readiness adapter missing", 5);
        }

        default void guardPassionEcuHardware(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Passion ECU hardware adapter missing", 5);
        }

        default void guardPassionAvmNexus(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Passion AVM Nexus adapter missing", 5);
        }

        default void guardPassionDisplayPower(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Passion per-display power adapter missing", 5);
        }

        default void guardPassionNexusPower(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Passion per-display power adapter missing", 5);
        }

        default void guardStockStatusBar(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Stock status-bar adapter missing", 5);
        }

        default void guardPassionStatusBar(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Passion stock status-bar adapter missing", 5);
        }

        default void guardPassionMusicDayDrawables(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED",
                    "Passion Music default drawable adapter missing",
                    5);
        }

        default void guardPassionMusicForeground(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED",
                    "Passion Music foreground channel adapter missing",
                    5);
        }

        default void guardPassionMusicInsets(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED",
                    "Passion Music main-activity inset adapter missing",
                    5);
        }

        default void guardPassionUsbMediaSession(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Passion USB media session adapter missing", 5);
        }

        default void guardPassionCarSettingsRecycler(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED",
                    "Passion CarSettings recycler adapter missing",
                    5);
        }

        default void guardPassionSettingsBluetoothRole(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED",
                    "Passion Settings Bluetooth role adapter missing",
                    5);
        }

        default void guardPassionSettingsSound(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Passion Settings Sound adapter missing", 5);
        }

        default void guardPassionSettingsUnits(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Passion Settings Units adapter missing", 5);
        }

        default void guardPassionCameraVirtual(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Passion virtual camera adapter missing", 5);
        }

        default void guardPassionDvrVirtual(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Passion virtual DVR adapter missing", 5);
        }

        default void guardPassionPhone(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Passion synthetic phone adapter missing", 5);
        }

        default void guardPassionAmbientLight(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Passion ambient service adapter missing", 5);
        }

        default void guardPassionLauncherAudio(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Passion Launcher audio adapter missing", 5);
        }

        default void guardPassionBluetoothVolume(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Passion Bluetooth volume adapter missing", 5);
        }

        default void guardPassionServiceForeground(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED",
                    "Passion service foreground channel adapter missing",
                    5);
        }

        default void guardPassionStartupVolume(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED",
                    "Passion startup volume channel adapter missing",
                    5);
        }

        default void guardPassionVehicleAnimation(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED",
                    "Passion Electric animation cleanup adapter missing",
                    5);
        }

        default void guardPassionVehicleAudio(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED",
                    "Passion nullable automotive audio adapter missing",
                    5);
        }

        default void guardPassionVehicleBluetooth(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED",
                    "Passion vehicle Bluetooth listener adapter missing",
                    5);
        }

        default void guardPassionBluetoothRole(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED",
                    "Passion optional Bluetooth role adapter missing",
                    5);
        }

        default void guardPassionSidebars(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Passion sidebar adapter missing", 5);
        }

        default void guardPassionDiagnostics(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED",
                    "Passion optional diagnostics adapter missing",
                    5);
        }

        default void routeSystemUi(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "SystemUI routing adapter missing", 5);
        }

        default void guardHiboardStartup(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Hiboard startup adapter missing", 5);
        }

        default void routeHiboard(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Hiboard routing adapter missing", 5);
        }

        default void routeRearScreen(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "ThirdScreen rear display adapter missing", 5);
        }

        default void guardSecondaryNavigationDecorated(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED",
                    "Profile-bound clean secondary navigation adapter missing",
                    5);
        }

        default void guardSecondaryNavigationSuppressed(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Clean secondary navigation adapter missing", 5);
        }

        default void guardStartupVolume(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Startup volume adapter missing", 5);
        }

        default void guardHiboardTopGesture(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "OEM top-gesture adapter missing", 5);
        }

        default void guardInterguestStartup(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Interguest Ethernet startup missing", 5);
        }

        default void guardKeyIngress(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Authenticated OEM ingress adapter missing", 5);
        }

        default void guardKeyManager(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "OEM KeyManager adapter missing", 5);
        }

        default void guardSystemPolicyHardware(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "OEM SystemPolicy hardware adapter missing", 5);
        }

        default void guardNetwork(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Network adapter missing", 5);
        }

        default void guardCameraVirtual(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Camera virtual adapter missing", 5);
        }

        default void guardOriginalNaviVin(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Original Navi VIN adapter missing", 5);
        }

        default void guardOriginalNaviVehicle(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Original Navi vehicle adapter missing", 5);
        }

        default void guardOriginalNaviEgl(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Original Navi EGL adapter missing", 5);
        }

        default void guardOriginalNaviLibraries(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Original Navi libraries adapter missing", 5);
        }

        default void guardNaviEgl(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Navi EGL adapter missing", 5);
        }

        default void guardNaviVin(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Navi VIN adapter missing", 5);
        }

        default void guardNaviVehicle(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "H97C Navi adapter missing", 5);
        }

        default void guardNaviVersion(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Navi version adapter missing", 5);
        }

        default void guardMediaCloudAvailability(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "CloudFM availability adapter missing", 5);
        }

        default void guardApi28UsbPlayer() throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED",
                    "Free A9 USB platform decoder adapter missing",
                    5);
        }

        default void guardUsbPlayer(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "USB platform decoder adapter missing", 5);
        }

        default void guardMusicFullscreen(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Music fullscreen adapter missing", 5);
        }

        default void guardGalleryArm64(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Gallery ARM64 adapter missing", 5);
        }

        default void guardMusicPage(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Optional Music page adapter missing", 5);
        }

        default void guardAqyProvider(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Optional AQY provider adapter missing", 5);
        }

        default void guardLauncherDvr(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Launcher DVR adapter missing", 5);
        }

        default void guardLauncherCanbus(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Launcher CAN startup adapter missing", 5);
        }

        default void guardLauncherPower(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Launcher power adapter missing", 5);
        }

        default void guardSettingsVoiceClone(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Optional VoiceClone adapter missing", 5);
        }

        default void guardSettingsVoiceMute(boolean apply) throws Exception {
            throw new Failure(
                    "PATCH_OPERATION_UNSUPPORTED", "Settings voice mute adapter missing", 5);
        }

        default void guardStrPower(boolean apply) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "STR power adapter missing", 5);
        }

        default Path stepRoot() {
            throw new Failure(
                    "PATCH_SOURCE_MISSING", "Explicit recipe source directory required", 5);
        }

        default String originalSha256() {
            throw new Failure("PATCH_SOURCE_MISSING", "Original APK pin required", 5);
        }

        /** The SHA-256 that original of the profile declares for the patch input. */
        default String originalPin() {
            throw new Failure("PATCH_SOURCE_MISSING", "Profile original pin required", 5);
        }

        /** The SHA-256 that original of the profile declares for an original:NAME reference. */
        default String originalPin(String reference) {
            throw new Failure("PATCH_SOURCE_MISSING", "Profile original pin required", 5);
        }

        /** True when the input is an intermediate artifact our code builds from pinned inputs. */
        default boolean derivedInput() {
            return false;
        }

        default void useServices(List<String> names) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Service manifest adapter missing", 5);
        }

        default void mergeClasses(String reference, Map<String, Object> pins) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Class merge adapter missing", 5);
        }

        default void donorSlot(String reference, Map<String, Object> pins) throws Exception {
            throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Donor DEX slot adapter missing", 5);
        }
    }

    static void apply(String name, Map<String, Object> metadata, Target target) throws Exception {
        if (name.equals("selinux-canbus-nxp.json")
                || name.equals("selinux-bounded-su-nxp.json")
                || name.equals("selinux-service-contexts-nxp.json")
                || name.equals("selinux-hash-nxp.json")) {
            Failure.require(
                    target.api() == 28,
                    "PATCH_PROFILE",
                    "Exact NXP API28 canbus policy profile required",
                    5);
        }

        if (target.api() == 30) {
            genericApi30(name, metadata, target, true);

            return;
        }

        Failure.require(
                target.api() == 28,
                "PATCH_API_UNSUPPORTED",
                "This recipe family requires Android API 28",
                5);
        Map<String, Object> constants = Json.obj(metadata, "constants");

        switch (name) {
            case "shutdown-router-a9.json" ->
                    RecipesQinggan09.shutdownRouter(name, metadata, constants, target);
            case "nxp-systemui-media-library.json", "nxp-systemui-settings-managers.json" ->
                    RecipesNxp.systemUiDonors(name, metadata, target);
            case "nxp-systemui-display-routing.json" ->
                    RecipesNxp.systemUiRouting(name, metadata, target);
            case "nxp-music-media-compat.json" -> RecipesNxp.musicMedia(name, metadata, target);
            case "nxp-launcher-display-routing.json" ->
                    RecipesNxp.launcherRouting(name, metadata, target);
            case "nxp-systempolicy-hardware.json" ->
                    RecipesNxp.systemPolicyHardware(name, metadata, constants, target);
            case "systemservice-network.json" ->
                    NetworkGuard.apply(
                            target.constants(),
                            target.api(),
                            target.originalSha256(),
                            target.dex());
            case "systempolicy-hardware.json" ->
                    SystemPolicyHardwareGuard.apply(
                            target.constants(),
                            target.api(),
                            target.originalSha256(),
                            target.dex());
            case "nxp-settings-voice-initialization.json" ->
                    RecipesNxp.settingsVoiceInitialization(name, metadata, constants, target);
            case "nxp-phone-provider.json" -> RecipesNxp.phoneProvider(target);
            case "nxp-phone-connect-anr.json" ->
                    RecipesNxp.phoneConnectAnr(name, metadata, constants, target);
            case "phone-provider.json" -> RecipesQinggan09.phoneProvider(target);
            case "nxp-voice-arm64-natives.json" -> RecipesNxp.voiceNatives(constants, target);
            case "add-qg-runtime.json" ->
                    target.putEntry(
                            use(target, "component:qg-runtime"),
                            "lib/" + target.abi() + "/libqg_runtime.so");
            case "qgime-arm64-natives.json" -> RecipesQinggan09.qgimeNatives(target);
            case "launcher-display-routing/launcher-display-routing.json" ->
                    RecipesQinggan09.launcherRouting(constants, target);
            case "media-card-music-fix.json" -> RecipesQinggan09.mediaCardMusic(target);
            case "usercenter-network/usercenter-network.json" ->
                    RecipesQinggan09.usercenterNetwork(target);
            case "selinux-canbus-nxp.json",
                    "selinux-service-contexts-nxp.json",
                    "selinux-hash-nxp.json" ->
                    RecipesNxp.selinuxPolicy(name, constants, target);
            case "selinux-hash.json" -> RecipesQinggan09.selinuxHash(target);
            case "selinux-bounded-su-a9.json", "selinux-bounded-su-nxp.json" ->
                    bounded(target, constants);
            case "selinux-canbus-a9.json" -> RecipesQinggan09.selinuxCanbus(constants, target);
            case "can-early-policy-a9.json" ->
                    RecipesQinggan09.canEarlyPolicy(metadata, constants, target);
            case "can-early-file-contexts-a9.json" ->
                    RecipesQinggan09.canEarlyFileContexts(metadata, constants, target);
            case "can-early-property-contexts-a9.json" ->
                    RecipesQinggan09.canEarlyPropertyContexts(metadata, constants, target);
            case "can-simulator-policy-a9.json" ->
                    RecipesQinggan09.canSimulatorPolicy(metadata, constants, target);
            case "can-simulator-contexts-a9.json" ->
                    RecipesQinggan09.canSimulatorContexts(metadata, constants, target);
            case "selinux-property-contexts-a9.json", "selinux-service-contexts-a9.json" ->
                    contexts(target, constants, name.contains("property"));
            case "systemui-display-routing.json" -> systemUi(target);
            case "settingnetwork-framework-gaps.json" ->
                    RecipesQinggan09.settingNetworkGaps(name, metadata, target);
            case "media-runtime.json" -> media(metadata, target);
            case "api28-usb-player.json" -> RecipesQinggan09.usbPlayer(metadata, constants, target);
            case "camera-capabilities.json" -> CameraRecipe.apply(target);
            case "api28-gallery-arm64.json" -> target.guardGalleryArm64(true);
            case "systemservice-framework.json" -> RecipesQinggan09.systemServiceFramework(target);
            default -> literalCalls(name, metadata, target);
        }
    }

    private static void systemUi(Target target) throws Exception {
        String phone = "Lcom/qinggan/systemui/statusbar/phone/PhoneStatusBar;->";
        String add = "addView(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                remove = "removeView(Landroid/view/View;)V";

        method(
                target,
                phone + "addStatusBarWindow(Z)V",
                windowRouter("{p1, v0, v1}", add),
                windowRouter("{p1, v0}", remove));
        method(
                target,
                phone + "addStatusBarWindowSecond(Z)V",
                windowRouter("{p1, v0, v1}", add),
                windowRouter("{p1, v0}", remove));
        method(target, phone + "showMuteIcon(Z)V", windowRouter("{p1, v0, v1}", add));
        method(
                target,
                phone + "setNavigationBarVisibility(Z)V",
                windowRouter("{v2, v3, v1}", add),
                windowRouter("{v1, v2}", remove));
        method(
                target,
                phone + "destroy()V",
                windowRouter("{v0, v1}", "removeViewImmediate(Landroid/view/View;)V"));
        target.addDex("launcher-display-routing/src");
    }

    /** Routes one WindowManager call through the static WindowRouter of the same name. */
    private static DexEdits.Edit windowRouter(String registers, String call) {
        String manager = call.startsWith("removeViewImmediate") ? "WindowManager" : "ViewManager";

        return DexEdits.Edit.replace(
                List.of(
                        "invoke-interface "
                                + registers
                                + ", Landroid/view/WindowManager;->"
                                + call),
                List.of(
                        "invoke-static "
                                + registers
                                + ", Lcom/voyemu/output/WindowRouter;->"
                                + call.replace("(", "(Landroid/view/" + manager + ";")));
    }

    private static void media(Map<String, Object> metadata, Target target) throws Exception {
        require(target.abi().equals("arm64-v8a"), "Media adapter requires ARM64");
        Map<String, Object> contract = Json.obj(metadata, "contract");
        require(
                Json.integer(contract, "schema") == 1
                        && Json.str(contract, "original_sha256").equals(target.originalSha256()),
                "Media original/contract mismatch");
        List<String> libraries =
                Json.list(contract.get("shared_libraries")).stream()
                        .map(value -> (String) value)
                        .toList();
        require(
                libraries.equals(List.of("QGAPI", "QGMOD", "QGVehicle")),
                "Unexpected media shared libraries");
        DexEdits.MethodEdit application =
                DexEdits.MethodEdit.parse(
                        Json.list(Json.obj(metadata, "constants").get("applicationEdit")));
        require(
                application.method().equals("Lcom/qinggan/media/MediaApplication;->onCreate()V"),
                "Expected OEM MediaApplication.onCreate");
        target.useLibraries(libraries);
        target.mergeClasses("component:media", Json.obj(contract, "browser_classes"));
        target.useServices(
                Json.list(contract.get("added_browser_services")).stream()
                        .map(value -> (String) value)
                        .toList());
        method(target, application);

        for (Object value : Json.list(contract.get("removed_arm32_libraries"))) {
            String library = (String) value;
            require(
                    library.matches("lib/armeabi-v7a/[A-Za-z0-9_]+\\.so"),
                    "Unexpected media native path");
            target.removeEntry(library);
        }
    }

    static void validateApi30(String name, Map<String, Object> metadata, Target target)
            throws Exception {
        genericApi30(name, metadata, target, false);
    }

    private static void genericApi30(
            String name, Map<String, Object> metadata, Target target, boolean apply)
            throws Exception {
        require(
                !REGISTERED.contains(name) && !EXPLICIT_HANDLERS.contains(name),
                "API28 handler cannot run as API30 generic recipe");
        require(
                Json.integer(metadata, "api") == 30
                        && Json.str(metadata, "sourceSha256").matches("[0-9a-f]{64}"),
                "Pinned API30 recipe required");
        Map<String, Object> constants = Json.obj(metadata, "constants");
        List<Object> pins = Json.list(constants.get("original_sha256_allowlist"));
        String actual = target.originalSha256();
        // An intermediate artifact follows from pinned inputs and own sources; pinning its bytes
        // would pin own repository files, so exactly such inputs carry an empty allowlist.
        require(
                actual.matches("[0-9a-f]{64}")
                        && pins.size() <= 8
                        && new HashSet<>(pins).size() == pins.size()
                        && pins.stream()
                                .allMatch(pin ->
                                        pin instanceof String text && text.matches("[0-9a-f]{64}"))
                        && pins.isEmpty() == target.derivedInput()
                        && (pins.isEmpty() || pins.contains(actual)),
                "API30 original APK pin mismatch");
        List<Object> calls = Json.list(metadata.get("calls"));
        require(!calls.isEmpty() && calls.size() <= 32, "Bounded API30 operations required");
        RecipesApi30.requireExactFrameworkAndKeyRecipes(name, target, pins, calls);
        RecipesApi30.requireExactNavigationHiboardAudioRecipes(name, target, pins, calls);
        RecipesApi30.requireExactPassionRecipes(name, pins, calls);
        // Validate the complete operation list and source entries before mutating the APK.
        RecipesApi30 validation = new RecipesApi30(name, metadata, target, pins, calls);

        for (Object value : calls) {
            validation.validate(value);
        }

        if (!apply) {
            return;
        }

        RecipesApi30.applyOperations(target, calls);
    }

    static void literalCalls(String name, Map<String, Object> metadata, Target target)
            throws Exception {
        List<Object> calls = Json.list(metadata.get("calls"));

        Failure.require(
                !calls.isEmpty(),
                "PATCH_HANDLER_MISSING",
                "Recipe " + name + " has no registered operations",
                5);

        for (Object item : calls) {
            Map<String, Object> call = Json.object(item);
            List<Object> args = Json.list(call.get("args"));

            switch (Json.str(call, "operation")) {
                case "editLines" ->
                        edit(
                                target,
                                (String) args.get(0),
                                Json.list(args.get(1)),
                                (String) args.get(2));
                case "editMethod" -> method(target, DexEdits.MethodEdit.parse(args));
                case "addDex" -> target.addDex((String) args.get(0));
                case "removeEntry" -> target.removeEntry((String) args.get(0));
                case "useLibraries" ->
                        target.useLibraries(
                                Json.list(args.get(0)).stream()
                                        .map(itemName -> (String) itemName)
                                        .toList());
                case "mergeClasses" ->
                        target.mergeClasses((String) args.get(0), Json.object(args.get(1)));
                case "donorSlot" ->
                        target.donorSlot((String) args.get(0), Json.object(args.get(1)));
                default ->
                        throw new Failure(
                                "PATCH_OPERATION_UNSUPPORTED",
                                "Unsupported compiled recipe operation",
                                5);
            }
        }
    }

    private static void bounded(Target target, Map<String, Object> constants) throws Exception {
        Path file = output(target);
        String text = Files.readString(file);
        String marker = Json.str(constants, "MARKER"),
                permissive = Json.str(constants, "PERMISSIVE");

        require(
                !text.contains(marker) && text.contains(permissive),
                "Bounded policy source differs");
        Set<String> known = new HashSet<>();
        var matcher =
                Pattern.compile("\\((?:type|typealias|typeattribute) ([\\w-]+)\\)").matcher(text);

        while (matcher.find()) {
            known.add(matcher.group(1));
        }

        List<String> applied = new java.util.ArrayList<>();

        // The vendor policy names platform types through the 28.0 mapping, where X_28_0 is the
        // attribute of X alone; unversioned names are platform attributes or vendor types.
        for (String use : List.of("original:PlatSepolicy", "original:VendorSepolicy")) {
            boolean mapped = use.equals("original:VendorSepolicy");
            Pattern allow =
                    Pattern.compile(
                            mapped
                                    ? "^\\(allow su_28_0 ([\\w-]+) (?=\\()"
                                    : "^\\(allow su ([\\w-]+) (?=\\()");

            for (String line : Files.readString(use(target, use)).split("\n", -1)) {
                var rule = allow.matcher(line);

                if (!rule.find()) {
                    continue;
                }

                String type = rule.group(1);

                if (mapped && type.endsWith("_28_0")) {
                    type = type.substring(0, type.length() - 5);
                }

                // self names the subject itself, a valid target in every CIL policy.
                if (type.equals("self") || known.contains(type)) {
                    applied.add("(allow su " + type + " " + line.substring(rule.end()));
                }
            }
        }

        applied.addAll(
                List.of(
                        "(allow su system_data_file (file (relabelfrom)))",
                        "(allow su system_app_data_file (file (relabelto setattr unlink open write"
                                + " append)))",
                        "(allow su wallpaper_file (file (relabelto setattr open write append)))",
                        "(allow su system_server (binder (call)))",
                        "(allow su traced_enabled_prop (property_service (set)))"));
        Files.writeString(
                file,
                newline(text.replace(permissive, ""))
                        + "\n"
                        + marker
                        + "\n"
                        + String.join("\n", applied)
                        + "\n");
    }

    private static void contexts(Target target, Map<String, Object> constants, boolean properties)
            throws Exception {
        Path file = output(target);
        String text = Files.readString(file);
        List<String> entries =
                Json.list(constants.get("ENTRIES")).stream().map(value -> (String) value).toList();

        for (String entry : entries) {
            if (properties) {
                String prefix = entry.split(" ", -1)[0];

                for (String line : text.split("\n", -1)) {
                    require(
                            !line.trim().split("\\s+", -1)[0].equals(prefix),
                            "Property context already applied");
                }
            } else {
                require(!text.contains(entry), "Service context already applied");
            }
        }

        Files.writeString(file, newline(text) + String.join("\n", entries) + "\n");
    }

    /** Replaces the OEM connectivity probe with the own one for mobile (1) or Wi-Fi (0) data. */
    static void network(Target target, String sdk, int mobile) throws Exception {
        String owner = "Lcom/qinggan/" + sdk + "/NetUtil;";
        String manager =
                "sget-object v0, " + owner + "->connManager:Landroid/net/ConnectivityManager;";

        method(
                target,
                owner + "->isNetworkConnected(Landroid/content/Context;)Z",
                DexEdits.Edit.tail(
                        List.of(
                                manager,
                                "invoke-virtual {v0}, Landroid/net/ConnectivityManager;"
                                        + "->getActiveNetworkInfo()Landroid/net/NetworkInfo;"),
                        List.of(
                                manager,
                                "const/4 v1, 0x" + mobile,
                                "invoke-static {v0, v1}, Lcom/voyemu/oem/NetworkConnectivity;"
                                        + "->isConnected(Landroid/net/ConnectivityManager;Z)Z",
                                "move-result v0",
                                "sput-boolean v0, " + owner + "->mLastNetConnected:Z",
                                "return v0")));
    }

    /** Wraps one call in a try block whose NoSuchMethodError handler continues after it. */
    static DexEdits.Edit guardedCall(String call, String name) {
        String start = ":try_start_voyemu_" + name,
                end = ":try_end_voyemu_" + name,
                handler = ":catch_voyemu_" + name;

        return DexEdits.Edit.replace(
                List.of(call),
                List.of(
                        start,
                        call,
                        end,
                        ".catch Ljava/lang/NoSuchMethodError; {"
                                + start
                                + " .. "
                                + end
                                + "} "
                                + handler,
                        handler));
    }

    /** Line edits of the text output of a file node. */
    static void edit(Target target, String file, List<Object> edits, String hash) throws Exception {
        require(file.equals("output"), "Line edits apply only to a text output");
        SourceEdits.file(output(target), edits, hash);
    }

    static void method(Target target, DexEdits.MethodEdit edit) {
        target.dex().edit(List.of(edit));
    }

    static void method(Target target, String method, DexEdits.Edit... edits) {
        method(target, new DexEdits.MethodEdit(method, null, List.of(edits)));
    }

    static String newline(String text) {
        return text.replaceFirst("\\n*$", "\n");
    }

    static Path output(Target target) {
        Failure.require(
                target.output() != null, "PATCH_OUTPUT_MISSING", "Recipe requires file output", 5);

        return target.output();
    }

    static Path use(Target target, String name) {
        Path path = target.uses().get(name);

        Failure.require(path != null, "PATCH_DEPENDENCY_MISSING", "Recipe requires " + name, 5);

        return path;
    }

    static void require(boolean condition, String message) {
        Failure.require(condition, "PATCH_ASSERTION_FAILED", message, 5);
    }
}
