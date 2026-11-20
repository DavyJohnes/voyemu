package com.voyemu.prepare;

import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** API30 generic recipe shape checks, operation validation and application. */
final class RecipesApi30 {
    private final String name;
    private final Map<String, Object> metadata;
    private final Recipes.Target target;
    private final List<Object> pins;
    private final List<Object> calls;
    private final Set<String> removed = new HashSet<>();
    private boolean guardedNetwork,
            guardedStrPower,
            guardedLauncherPower,
            guardedDiagnostics,
            guardedSidebars,
            guardedBluetoothRole,
            guardedMusicForeground,
            guardedMusicDayDrawables,
            guardedAqyProvider,
            routedFreeSystemUi,
            routedFreeHiboard,
            routedRearScreen,
            guardedKeyManager,
            guardedKeyIngress,
            guardedSecondaryNavigation,
            guardedPlatformNavigation,
            guardedTopGesture,
            guardedSystemPolicyHardware;

    RecipesApi30(
            String name,
            Map<String, Object> metadata,
            Recipes.Target target,
            List<Object> pins,
            List<Object> calls) {
        this.name = name;
        this.metadata = metadata;
        this.target = target;
        this.pins = pins;
        this.calls = calls;
    }

    static void requireExactFrameworkAndKeyRecipes(
            String name, Recipes.Target target, List<Object> pins, List<Object> calls) {
        if (name.equals("api30-framework-libraries.json")) {
            require(
                    calls.equals(
                            List.of(
                                    Map.of(
                                            "operation",
                                            "useLibraries",
                                            "args",
                                            List.of(List.of("QGAPI"))))),
                    "Exact QGAPI library recipe required");
        }

        if (name.equals(PassionVehicleAnimationGuard.NAME + ".json")) {
            require(
                    pins.equals(List.of(PassionVehicleAnimationGuard.PIN))
                            && calls.equals(
                                    List.of(
                                            Map.of(
                                                    "operation",
                                                    "guardPassionVehicleAnimation",
                                                    "args",
                                                    List.of()))),
                    "Exact named Passion Electric animation cleanup operation required");
        }

        if (name.equals(KeyManagerGuard.NAME + ".json")) {
            require(
                    calls.size() == 2
                            && calls.get(0)
                                    .equals(
                                            Map.of(
                                                    "operation",
                                                    "guardKeyManager",
                                                    "args",
                                                    List.of()))
                            && "useLibraries".equals(Json.object(calls.get(1)).get("operation")),
                    "Exact OEM KeyManager guard and library dependency order required");
        }

        if (name.equals(SystemPolicyHardwareGuard.NAME + ".json")) {
            require(
                    calls.equals(
                            List.of(
                                    Map.of(
                                            "operation",
                                            "guardSystemPolicyHardware",
                                            "args",
                                            List.of()),
                                    Map.of(
                                            "operation",
                                            "useLibraries",
                                            "args",
                                            List.of(SystemPolicyHardwareGuard.API30_LIBRARIES)))),
                    "Exact OEM SystemPolicy guard and library dependency order required");
        }

        if (name.equals(KeyManagerIngressGuard.NAME + ".json")) {
            require(
                    calls.equals(
                            List.of(Map.of("operation", "guardKeyIngress", "args", List.of()))),
                    "Exact authenticated OEM ingress operation required");
        }
    }

    static void requireExactNavigationHiboardAudioRecipes(
            String name, Recipes.Target target, List<Object> pins, List<Object> calls) {
        if (name.equals(SecondaryNavigationGuard.NAME + ".json")) {
            require(
                    calls.equals(
                            List.of(
                                    Map.of(
                                            "operation",
                                            "guardSecondaryNavigationSuppressed",
                                            "args",
                                            List.of()),
                                    Map.of(
                                            "operation",
                                            "guardHiboardTopGesture",
                                            "args",
                                            List.of()),
                                    Map.of("operation", "guardStartupVolume", "args", List.of()),
                                    Map.of(
                                            "operation",
                                            "guardInterguestStartup",
                                            "args",
                                            List.of()))),
                    "Exact ordered Free A11 7.3.1 platform operations" + " required");
        }

        if (name.equals(PassionSecondaryNavigationGuard.NAME + ".json")) {
            require(
                    calls.equals(
                            List.of(
                                    Map.of(
                                            "operation",
                                            "guardSecondaryNavigationDecorated",
                                            "args",
                                            List.of()))),
                    "Exact Passion clean secondary navigation operation required");
        }

        if (name.equals("api30-hiboard-libraries.json")) {
            require(
                    calls.equals(
                            List.of(
                                    Map.of(
                                            "operation",
                                            "useLibraries",
                                            "args",
                                            List.of(List.of("QGAPI", "QGMOD", "QGVehicle"))))),
                    "Exact OEM Hiboard framework dependencies required");
        }

        if (name.equals("api30-hiboard-startup.json")) {
            require(
                    calls.equals(
                            List.of(Map.of("operation", "guardHiboardStartup", "args", List.of()))),
                    "Exact Hiboard startup operation required");
        }

        if (name.equals(HiboardRouting.NAME + ".json")) {
            require(
                    calls.equals(List.of(Map.of("operation", "routeHiboard", "args", List.of()))),
                    "Exact OEM Hiboard routing operation required");
        }

        if (name.equals(ThirdScreenRouting.NAME + ".json")) {
            require(
                    calls.equals(
                            List.of(Map.of("operation", "routeRearScreen", "args", List.of()))),
                    "Exact OEM ThirdScreen rear display operation required");
        }

        if (name.equals("api30-audio-policy.json")) {
            require(
                    calls.equals(
                            List.of(
                                    Map.of("operation", "guardAudioPolicy", "args", List.of()),
                                    Map.of(
                                            "operation",
                                            "useLibraries",
                                            "args",
                                            List.of(List.of("QGAPI"))))),
                    "Exact ordered AudioPolicy guard and QGAPI dependency required");
        }
    }

    static void requireExactPassionRecipes(String name, List<Object> pins, List<Object> calls) {
        if (name.equals(PassionMusicInsetsGuard.NAME + ".json")) {
            require(
                    calls.equals(
                            List.of(
                                    Map.of(
                                            "operation",
                                            "guardPassionMusicInsets",
                                            "args",
                                            List.of()))),
                    "Exact Music main-activity inset guard operation required");
        }

        if (name.equals(PassionUsbMediaSessionGuard.NAME + ".json")) {
            require(
                    calls.equals(
                            List.of(
                                    Map.of(
                                            "operation",
                                            "guardPassionUsbMediaSession",
                                            "args",
                                            List.of()))),
                    "Exact Passion USB media session operation required");
        }

        if (name.equals(PassionCarSettingsRecyclerGuard.NAME + ".json")) {
            require(
                    calls.equals(
                            List.of(
                                    Map.of(
                                            "operation",
                                            "guardPassionCarSettingsRecycler",
                                            "args",
                                            List.of()))),
                    "Exact Passion CarSettings recycler operation required");
        }

        if (name.equals(PassionSettingsBluetoothRoleGuard.NAME + ".json")) {
            require(
                    calls.equals(
                            List.of(
                                    Map.of(
                                            "operation",
                                            "guardPassionSettingsBluetoothRole",
                                            "args",
                                            List.of()))),
                    "Exact Passion Settings Bluetooth role operation required");
        }

        if (name.equals(PassionSettingsSoundGuard.NAME + ".json")) {
            require(
                    calls.equals(
                            List.of(
                                    Map.of(
                                            "operation",
                                            "guardPassionSettingsSound",
                                            "args",
                                            List.of()))),
                    "Exact Passion Settings Sound operation required");
        }

        if (name.equals(PassionSettingsUnitsGuard.NAME + ".json")) {
            require(
                    calls.equals(
                            List.of(
                                    Map.of(
                                            "operation",
                                            "guardPassionSettingsUnits",
                                            "args",
                                            List.of()))),
                    "Exact Passion Settings Units operation required");
        }

        if (name.equals(PassionCameraVirtualGuard.NAME + ".json")) {
            require(
                    calls.equals(
                            List.of(
                                    Map.of(
                                            "operation",
                                            "guardPassionCameraVirtual",
                                            "args",
                                            List.of()))),
                    "Exact Passion virtual Camera operation required");
        }

        if (name.equals(PassionPhoneGuard.NAME + ".json")) {
            require(
                    calls.equals(
                            List.of(Map.of("operation", "guardPassionPhone", "args", List.of()))),
                    "Exact Passion synthetic phone operation required");
        }
    }

    void validate(Object value) throws Exception {
        Map<String, Object> call = Json.object(value);
        List<Object> args = Json.list(call.get("args"));
        String operation = Json.str(call, "operation");

        if (validatePhoneAudioUsbOperations(operation, args)
                || validateStatusPowerOperations(operation, args)
                || validatePassionAppOperations(operation, args)
                || validateVehicleRoutingOperations(operation, args)
                || validateCameraNaviOperations(operation, args)
                || validateMediaLauncherOperations(operation, args)
                || validatePowerNavigationOperations(operation, args)
                || validateKeyNetworkEntryOperations(operation, args)) {
            return;
        }

        throw new Failure("PATCH_OPERATION_UNSUPPORTED", "Unsupported API30 generic operation", 5);
    }

    private boolean validatePhoneAudioUsbOperations(String operation, List<Object> args)
            throws Exception {
        switch (operation) {
            case "guardPhoneProvider" -> {
                require(
                        name.equals(PhoneProviderGuard.NAME + ".json")
                                && args.isEmpty()
                                && calls.size() == 2
                                && calls.get(0)
                                        .equals(
                                                Map.of(
                                                        "operation",
                                                        "guardPhoneProvider",
                                                        "args",
                                                        List.of()))
                                && "useLibraries"
                                        .equals(Json.object(calls.get(1)).get("operation")),
                        "Exact Phone provider recipe required");
                PhoneCallWindowGuard.policy(Json.obj(metadata, "constants"));
                PhoneProviderGuard.sourceInventory(Json.obj(metadata, "constants"));
                target.guardPhoneProvider(false);
            }
            case "guardAudioPolicy" -> {
                require(
                        name.equals("api30-audio-policy.json")
                                && args.isEmpty()
                                && calls.size() == 2,
                        "Exact AudioPolicy operation required");
                require(
                        Json.list(metadata.get("helperSources"))
                                .equals(AudioPolicyGuard.HELPER_SOURCES),
                        "Exact owned helper source inventory required");
                target.guardAudioPolicy(false);
            }
            case "guardAudioConsumer" -> {
                require(
                        name.equals("api30-audio-consumer.json")
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact AudioPolicy consumer operation required");
                target.guardAudioConsumer(false);
            }
            case "guardUsbPlayer" -> {
                require(
                        name.equals(UsbPlayerGuard.NAME + ".json")
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact USB player operation required");
                target.guardUsbPlayer(false);
            }
            case "guardMediaNotification" -> {
                require(
                        name.equals(MediaNotificationGuard.NAME + ".json")
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact notification operation required");
                target.guardMediaNotification(false);
            }
            default -> {
                return false;
            }
        }

        return true;
    }

    private boolean validateStatusPowerOperations(String operation, List<Object> args)
            throws Exception {
        switch (operation) {
            case "guardPassionMcuProperty" -> {
                require(
                        name.equals("passion-api30-mcu-property.json")
                                && pins.equals(List.of(PassionMcuPropertyGuard.PIN))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact pinned Passion guardPassionMcuProperty operation required");
                target.guardPassionMcuProperty(false);
            }
            case "guardPassionNavigationReady" -> {
                require(
                        name.equals("passion-api30-navigation-ready.json")
                                && pins.equals(List.of(PassionNavigationReadyGuard.APK))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact pinned Passion guardPassionNavigationReady operation required");
                target.guardPassionNavigationReady(false);
            }
            case "guardPassionStatusReady" -> {
                require(
                        name.equals("passion-api30-status-ready.json")
                                && pins.equals(List.of(PassionStatusReadyGuard.APK))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact pinned Passion guardPassionStatusReady operation required");
                target.guardPassionStatusReady(false);
            }
            case "guardPassionEcuHardware" -> {
                require(
                        name.equals("passion-api30-ecu-hardware.json")
                                && pins.equals(List.of(PassionGuestNullGuards.ECU.apk()))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact pinned Passion guardPassionEcuHardware operation required");
                target.guardPassionEcuHardware(false);
            }
            case "guardPassionAvmNexus" -> {
                require(
                        name.equals("passion-api30-avm-nexus.json")
                                && pins.equals(List.of(PassionGuestNullGuards.AVM.apk()))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact pinned Passion guardPassionAvmNexus operation required");
                target.guardPassionAvmNexus(false);
            }
            case "guardPassionDisplayPower" -> {
                require(
                        name.equals(PassionDisplayPowerGuard.NAME + ".json")
                                && pins.equals(List.of(PassionDisplayPowerGuard.PIN))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Passion pinned power operation required");
                target.guardPassionDisplayPower(false);
            }
            case "guardPassionNexusPower" -> {
                require(
                        name.equals(PassionNexusPowerGuard.NAME + ".json")
                                && pins.equals(List.of(PassionNexusPowerGuard.PIN))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Passion pinned power operation required");
                target.guardPassionNexusPower(false);
            }
            case "guardStockStatusBar" -> {
                require(
                        name.equals(StockStatusBarGuard.NAME + ".json")
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact stock status-bar operation required");
                target.guardStockStatusBar(false);
            }
            case "guardPassionStatusBar" -> {
                require(
                        name.equals(PassionStatusBarGuard.NAME + ".json")
                                && pins.equals(List.of(PassionStatusBarGuard.PIN))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Passion stock status-bar operation required");
                target.guardPassionStatusBar(false);
            }
            case "guardPassionMusicDayDrawables" -> {
                require(
                        name.equals(PassionMusicDayDrawableGuard.NAME + ".json")
                                && pins.equals(List.of(PassionMusicDayDrawableGuard.PIN))
                                && args.isEmpty()
                                && !guardedMusicDayDrawables
                                && calls.size() == 1,
                        "Exact Passion three default drawable operation required");
                guardedMusicDayDrawables = true;
                target.guardPassionMusicDayDrawables(false);
            }
            case "guardPassionMusicInsets" -> {
                require(
                        name.equals(PassionMusicInsetsGuard.NAME + ".json")
                                && pins.equals(List.of(PassionMusicInsetsGuard.PIN))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Passion Music main-activity inset operation required");
                target.guardPassionMusicInsets(false);
            }
            default -> {
                return false;
            }
        }

        return true;
    }

    private boolean validatePassionAppOperations(String operation, List<Object> args)
            throws Exception {
        switch (operation) {
            case "guardPassionUsbMediaSession" -> {
                require(
                        name.equals(PassionUsbMediaSessionGuard.NAME + ".json")
                                && pins.equals(List.of(PassionUsbMediaSessionGuard.APK))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Passion USB media session operation required");
                target.guardPassionUsbMediaSession(false);
            }
            case "guardPassionCarSettingsRecycler" -> {
                require(
                        name.equals(PassionCarSettingsRecyclerGuard.NAME + ".json")
                                && pins.equals(List.of(PassionCarSettingsRecyclerGuard.PIN))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Passion CarSettings recycler operation required");
                target.guardPassionCarSettingsRecycler(false);
            }
            case "guardPassionSettingsBluetoothRole" -> {
                require(
                        name.equals(PassionSettingsBluetoothRoleGuard.NAME + ".json")
                                && pins.equals(List.of(PassionSettingsBluetoothRoleGuard.PIN))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Passion Settings Bluetooth role operation required");
                target.guardPassionSettingsBluetoothRole(false);
            }
            case "guardPassionCameraVirtual" -> {
                require(
                        name.equals(PassionCameraVirtualGuard.NAME + ".json")
                                && pins.equals(List.of(PassionCameraVirtualGuard.APK))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Passion virtual Camera operation required");
                target.guardPassionCameraVirtual(false);
            }
            case "guardPassionPhone" -> {
                require(
                        name.equals(PassionPhoneGuard.NAME + ".json")
                                && pins.equals(List.of(PassionPhoneGuard.APK))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Passion synthetic phone operation required");
                target.guardPassionPhone(false);
            }
            case "guardPassionSettingsUnits" -> {
                require(
                        name.equals(PassionSettingsUnitsGuard.NAME + ".json")
                                && pins.equals(List.of(PassionSettingsUnitsGuard.APK))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Passion Settings Units operation required");
                target.guardPassionSettingsUnits(false);
            }
            case "guardPassionSettingsSound" -> {
                require(
                        name.equals(PassionSettingsSoundGuard.NAME + ".json")
                                && pins.equals(List.of(PassionSettingsSoundGuard.APK))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Passion Settings Sound operation required");
                target.guardPassionSettingsSound(false);
            }
            case "guardPassionMusicForeground" -> {
                require(
                        name.equals(PassionMusicForegroundGuard.NAME + ".json")
                                && pins.equals(List.of(PassionMusicForegroundGuard.PIN))
                                && args.isEmpty()
                                && !guardedMusicForeground,
                        "Exact Passion Music foreground operation required");
                guardedMusicForeground = true;
                target.guardPassionMusicForeground(false);
            }
            case "guardPassionAmbientLight" -> {
                require(
                        name.equals(PassionAmbientLightGuard.NAME + ".json")
                                && pins.equals(List.of(PassionAmbientLightGuard.PIN))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Passion optional ambient light operation required");
                target.guardPassionAmbientLight(false);
            }
            case "guardPassionLauncherAudio" -> {
                require(
                        name.equals(PassionLauncherAudioGuard.NAME + ".json")
                                && pins.equals(List.of(PassionLauncherAudioGuard.PIN))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Passion optional Launcher audio operation required");
                target.guardPassionLauncherAudio(false);
            }
            case "guardPassionBluetoothVolume" -> {
                require(
                        name.equals(PassionBluetoothVolumeGuard.NAME + ".json")
                                && pins.size() == 3
                                && new HashSet<>(pins).equals(PassionBluetoothVolumeGuard.PINS)
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Passion Bluetooth volume operation required");
                target.guardPassionBluetoothVolume(false);
            }
            case "guardPassionStartupVolume" -> {
                require(
                        name.equals(PassionStartupVolumeGuard.NAME + ".json")
                                && pins.size() == 1
                                && pins.get(0).equals(PassionStartupVolumeGuard.PIN)
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Passion startup volume operation required");
                target.guardPassionStartupVolume(false);
            }
            case "guardPassionServiceForeground" -> {
                require(
                        name.equals(PassionServiceForegroundGuard.NAME + ".json")
                                && pins.size() == 2
                                && new HashSet<>(pins).equals(PassionServiceForegroundGuard.PINS)
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Passion service foreground operation required");
                target.guardPassionServiceForeground(false);
            }
            default -> {
                return false;
            }
        }

        return true;
    }

    private boolean validateVehicleRoutingOperations(String operation, List<Object> args)
            throws Exception {
        switch (operation) {
            case "guardPassionVehicleAnimation" -> {
                require(
                        name.equals(PassionVehicleAnimationGuard.NAME + ".json")
                                && pins.equals(List.of(PassionVehicleAnimationGuard.PIN))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Passion Electric animation cleanup operation required");
                target.guardPassionVehicleAnimation(false);
            }
            case "guardPassionVehicleAudio" -> {
                require(
                        name.equals(PassionVehicleAudioGuard.NAME + ".json")
                                && pins.equals(List.of(PassionVehicleAudioGuard.PIN))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Passion nullable automotive audio operation required");
                target.guardPassionVehicleAudio(false);
            }
            case "guardPassionVehicleBluetooth" -> {
                require(
                        name.equals(PassionVehicleBluetoothGuard.NAME + ".json")
                                && pins.equals(List.of(PassionVehicleBluetoothGuard.PIN))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Passion vehicle optional Bluetooth listener operation required");
                target.guardPassionVehicleBluetooth(false);
            }
            case "guardPassionBluetoothRole" -> {
                require(
                        name.equals(PassionBluetoothRoleGuard.NAME + ".json")
                                && pins.size() == 2
                                && new HashSet<>(pins).equals(PassionBluetoothRoleGuard.PINS)
                                && args.isEmpty()
                                && !guardedBluetoothRole,
                        "Exact Passion optional Bluetooth role operation required");
                guardedBluetoothRole = true;
                target.guardPassionBluetoothRole(false);
            }
            case "guardPassionSidebars" -> {
                require(
                        name.equals(PassionSidebarGuard.NAME + ".json")
                                && pins.equals(List.of(PassionSidebarGuard.PIN))
                                && args.isEmpty()
                                && !guardedSidebars,
                        "Exact Passion two-sidebar operation required");
                guardedSidebars = true;
                target.guardPassionSidebars(false);
            }
            case "guardPassionDiagnostics" -> {
                require(
                        name.equals(PassionDiagnosticsGuard.NAME + ".json")
                                && pins.equals(List.of(PassionDiagnosticsGuard.PIN))
                                && args.isEmpty()
                                && !guardedDiagnostics,
                        "Exact Passion optional diagnostics operation required");
                guardedDiagnostics = true;
                target.guardPassionDiagnostics(false);
            }
            case "routeSystemUi" -> {
                require(
                        name.equals(SystemUiRouting.NAME + ".json")
                                && args.isEmpty()
                                && !routedFreeSystemUi,
                        "Exact two-status-bar routing operation required");
                routedFreeSystemUi = true;
                target.routeSystemUi(false);
            }
            case "guardHiboardStartup" -> {
                require(
                        name.equals("api30-hiboard-startup.json")
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Hiboard startup operation required");
                target.guardHiboardStartup(false);
            }
            case "routeHiboard" -> {
                require(
                        name.equals(HiboardRouting.NAME + ".json")
                                && args.isEmpty()
                                && !routedFreeHiboard
                                && calls.size() == 1
                                && "api30-hiboard-routing/src/com/voyemu/output/HiboardRouter.java"
                                        .equals(metadata.get("helper")),
                        "Exact OEM Hiboard routing operation required");
                routedFreeHiboard = true;
                target.routeHiboard(false);
            }
            case "routeRearScreen" -> {
                require(
                        name.equals(ThirdScreenRouting.NAME + ".json")
                                && args.isEmpty()
                                && !routedRearScreen
                                && calls.size() == 1
                                && "api30-thirdscreen-rear/src/com/voyemu/output/RearScreen.java"
                                        .equals(metadata.get("helper")),
                        "Exact OEM ThirdScreen rear display operation required");
                routedRearScreen = true;
                target.routeRearScreen(false);
            }
            default -> {
                return false;
            }
        }

        return true;
    }

    private boolean validateCameraNaviOperations(String operation, List<Object> args)
            throws Exception {
        switch (operation) {
            case "guardCameraVirtual" -> {
                require(
                        name.equals(CameraVirtualGuard.NAME + ".json")
                                && args.isEmpty()
                                && calls.size() == 1
                                && CameraVirtualGuard.HELPER_PATH.equals(metadata.get("helper")),
                        "Exact Camera virtual operation and helper required");
                target.guardCameraVirtual(false);
            }
            case "guardOriginalNaviVin" -> {
                require(
                        name.equals("original-navi-vin.json")
                                && pins.equals(
                                        List.of(
                                                "c178682be164be4872697dbb5accdc6ea28446941b6d681ddb736f2fdb627c07"))
                                && args.isEmpty()
                                && calls.size() == 1
                                && "api30-original-navi-identity/vin-src/com/voyemu/qgapi/BrokerVin.java"
                                        .equals(metadata.get("helper")),
                        "Exact original Navi VIN operation and helper required");
                target.guardOriginalNaviVin(false);
            }
            case "guardOriginalNaviVehicle" -> {
                require(
                        name.equals("original-navi-vehicle.json")
                                && pins.equals(
                                        List.of(
                                                "d95d1b05418660d278fbaf2e6e390f8849d3dd23555a38d8d1965efb85cccede"))
                                && args.isEmpty()
                                && calls.size() == 1
                                && "api30-original-navi-identity/vehicle-src/com/voyemu/qgapi/VehicleBroker.java"
                                        .equals(metadata.get("helper")),
                        "Exact original Navi vehicle operation and helper required");
                target.guardOriginalNaviVehicle(false);
            }
            case "guardOriginalNaviEgl" -> {
                require(
                        name.equals("original-navi-egl.json")
                                && pins.isEmpty()
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact original Navi EGL operation required");
                target.guardOriginalNaviEgl(false);
            }
            case "guardOriginalNaviLibraries" -> {
                require(
                        name.equals("original-navi-libraries.json")
                                && pins.isEmpty()
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact original Navi libraries operation required");
                target.guardOriginalNaviLibraries(false);
            }
            case "guardNaviEgl" -> {
                require(
                        name.equals(NaviEglGuard.NAME + ".json")
                                && pins.equals(List.of(NaviEglGuard.PIN))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Navi EGL operation required");
                target.guardNaviEgl(false);
            }
            case "guardNaviVin" -> {
                require(
                        name.equals(NaviVinGuard.NAME + ".json")
                                && pins.equals(List.of(NaviVinGuard.PIN))
                                && args.isEmpty()
                                && calls.size() == 1
                                && NaviVinGuard.HELPER_PATH.equals(metadata.get("helper")),
                        "Exact Navi VIN operation and helper required");
                target.guardNaviVin(false);
            }
            case "guardNaviVehicle" -> {
                require(
                        name.equals(NaviVehicleGuard.NAME + ".json")
                                && pins.equals(List.of(NaviVehicleGuard.PIN))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact H97C Navi operation required");
                target.guardNaviVehicle(false);
            }
            default -> {
                return false;
            }
        }

        return true;
    }

    private boolean validateMediaLauncherOperations(String operation, List<Object> args)
            throws Exception {
        switch (operation) {
            case "guardNaviVersion" -> {
                require(
                        name.equals(NaviVersionGuard.NAME + ".json")
                                && pins.equals(List.of(NaviVersionGuard.PIN))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Navi version operation required");
                target.guardNaviVersion(false);
            }
            case "guardMediaCloudAvailability" -> {
                require(
                        name.equals(MediaCloudConfigGuard.NAME + ".json")
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact unavailable CloudFM operation required");
                target.guardMediaCloudAvailability(false);
            }
            case "guardMusicFullscreen" -> {
                require(
                        name.equals(MusicFullscreenGuard.NAME + ".json")
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Music fullscreen operation required");
                target.guardMusicFullscreen(false);
            }
            case "guardMusicPage" -> {
                require(
                        name.equals(MusicPageGuard.NAME + ".json")
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact optional Music page operation required");
                target.guardMusicPage(false);
            }
            case "guardAqyProvider" -> {
                require(
                        name.equals(AqyProviderGuard.NAME + ".json")
                                && args.isEmpty()
                                && !guardedAqyProvider,
                        "Exact optional AQY provider operation required");
                guardedAqyProvider = true;
                target.guardAqyProvider(false);
            }
            case "guardLauncherDvr" -> {
                require(
                        name.equals(LauncherDvrGuard.NAME + ".json")
                                && args.isEmpty()
                                && calls.size() == 1
                                && LauncherDvrGuard.HELPER_PATH.equals(metadata.get("helper")),
                        "Exact Launcher DVR operation and helper required");
                target.guardLauncherDvr(false);
            }
            case "guardLauncherCanbus" -> {
                require(
                        name.equals(LauncherCanbusGuard.NAME + ".json")
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Launcher CAN startup operation required");
                target.guardLauncherCanbus(false);
            }
            default -> {
                return false;
            }
        }

        return true;
    }

    private boolean validatePowerNavigationOperations(String operation, List<Object> args)
            throws Exception {
        switch (operation) {
            case "guardLauncherPower" -> {
                require(
                        name.equals(LauncherPowerGuard.NAME + ".json")
                                && args.isEmpty()
                                && !guardedLauncherPower,
                        "Exact unsupported Launcher power operation required");
                guardedLauncherPower = true;
                target.guardLauncherPower(false);
            }
            case "guardSettingsVoiceClone" -> {
                require(
                        name.equals(SettingsVoiceCloneGuard.NAME + ".json")
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact optional VoiceClone operation required");
                target.guardSettingsVoiceClone(false);
            }
            case "guardSettingsVoiceMute" -> {
                require(
                        name.equals(SettingsVoiceMuteGuard.NAME + ".json")
                                && pins.equals(List.of(SettingsVoiceMuteGuard.APK))
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Settings voice mute operation required");
                target.guardSettingsVoiceMute(false);
            }
            case "guardStrPower" -> {
                require(
                        name.equals(StrPowerGuard.NAME + ".json")
                                && args.isEmpty()
                                && !guardedStrPower,
                        "Exact unsupported STR operation required");
                guardedStrPower = true;
                target.guardStrPower(false);
            }
            case "guardSecondaryNavigationDecorated" -> {
                require(
                        name.equals(PassionSecondaryNavigationGuard.NAME + ".json")
                                && pins.equals(List.of(PassionSecondaryNavigationGuard.PIN))
                                && args.isEmpty()
                                && !guardedPlatformNavigation,
                        "Exact Passion clean secondary navigation operation required");
                guardedPlatformNavigation = true;
                target.guardSecondaryNavigationDecorated(false);
            }
            case "guardSecondaryNavigationSuppressed" -> {
                require(
                        name.equals(SecondaryNavigationGuard.NAME + ".json")
                                && pins.equals(List.of(SecondaryNavigationGuard.PIN))
                                && args.isEmpty()
                                && !guardedSecondaryNavigation,
                        "Exact clean secondary navigation operation required");
                guardedSecondaryNavigation = true;
                target.guardSecondaryNavigationSuppressed(false);
            }
            case "guardHiboardTopGesture" -> {
                require(
                        name.equals(SecondaryNavigationGuard.NAME + ".json")
                                && pins.equals(List.of(HiboardTopGestureGuard.PIN))
                                && args.isEmpty()
                                && guardedSecondaryNavigation
                                && !guardedTopGesture,
                        "Exact top gesture requires its clean navigation predecessor");
                guardedTopGesture = true;
                target.guardHiboardTopGesture(false);
            }
            case "guardInterguestStartup" -> {
                require(
                        name.equals(SecondaryNavigationGuard.NAME + ".json")
                                && pins.equals(List.of(HiboardTopGestureGuard.PIN))
                                && args.isEmpty()
                                && guardedTopGesture,
                        "Exact interguest startup requires the Free A11 7.3.1 platform chain");
                target.guardInterguestStartup(false);
            }
            case "guardStartupVolume" -> {
                if (name.equals(StartupVolumeGuard.NAME + ".json")) {
                    require(
                            args.isEmpty() && calls.size() == 1,
                            "Exact clean startup-volume operation required");
                } else {
                    require(
                            name.equals(SecondaryNavigationGuard.NAME + ".json")
                                    && args.isEmpty()
                                    && guardedSecondaryNavigation
                                    && guardedTopGesture,
                            "Exact startup volumes require the complete platform chain");
                }

                target.guardStartupVolume(false);
            }
            default -> {
                return false;
            }
        }

        return true;
    }

    private boolean validateKeyNetworkEntryOperations(String operation, List<Object> args)
            throws Exception {
        switch (operation) {
            case "guardKeyManager" -> {
                require(
                        name.equals(KeyManagerGuard.NAME + ".json")
                                && args.isEmpty()
                                && !guardedKeyManager,
                        "Exact OEM KeyManager operation required");
                guardedKeyManager = true;
                target.guardKeyManager(false);
            }
            case "guardSystemPolicyHardware" -> {
                require(
                        name.equals(SystemPolicyHardwareGuard.NAME + ".json")
                                && args.isEmpty()
                                && !guardedSystemPolicyHardware,
                        "Exact OEM SystemPolicy hardware operation required");
                guardedSystemPolicyHardware = true;
                target.guardSystemPolicyHardware(false);
            }
            case "guardKeyIngress" -> {
                require(
                        name.equals(KeyManagerIngressGuard.NAME + ".json")
                                && args.isEmpty()
                                && !guardedKeyIngress,
                        "Exact authenticated OEM ingress operation required");
                guardedKeyIngress = true;
                target.guardKeyIngress(false);
            }
            case "guardNetwork" -> {
                require(
                        name.equals(NetworkGuard.NAME + ".json")
                                && args.isEmpty()
                                && !guardedNetwork,
                        "Exact network operation required");
                guardedNetwork = true;
                target.guardNetwork(false);
            }
            case "guardGalleryArm64" -> {
                require(
                        name.equals(GalleryArm64Libraries.NAME + ".json")
                                && args.isEmpty()
                                && calls.size() == 1,
                        "Exact Gallery ARM64 operation required");
                target.guardGalleryArm64(false);
            }
            case "useLibraries" -> {
                require(args.size() == 1, "Library arguments differ");
                List<Object> names = Json.list(args.get(0));
                require(
                        !names.isEmpty()
                                && names.size() <= 16
                                && names.stream()
                                        .allMatch(n ->
                                                n instanceof String text
                                                        && text.matches(
                                                                "[A-Za-z][A-Za-z0-9_.]{0,127}")),
                        "Bounded library names required");
            }
            case "removePinnedEntry" -> {
                require(
                        args.size() == 2
                                && args.get(0) instanceof String
                                && args.get(1) instanceof String,
                        "Pinned native arguments required");
                String entry = (String) args.get(0), pin = (String) args.get(1);
                require(
                        entry.matches("lib/armeabi-v7a/[A-Za-z0-9_]+\\.so")
                                && pin.matches("[0-9a-f]{64}"),
                        "Pinned ARM32 native entry required");
                require(removed.add(entry), "Duplicate native removal");
                target.verifyPinnedEntry(entry, pin);
            }
            default -> {
                return false;
            }
        }

        return true;
    }

    static void applyOperations(Recipes.Target target, List<Object> calls) throws Exception {
        for (Object value : calls) {
            Map<String, Object> call = Json.object(value);
            List<Object> args = Json.list(call.get("args"));

            switch (Json.str(call, "operation")) {
                case "useLibraries" ->
                        target.useLibraries(
                                Json.list(args.get(0)).stream().map(n -> (String) n).toList());
                case "routeSystemUi" -> target.routeSystemUi(true);
                case "guardHiboardStartup" -> target.guardHiboardStartup(true);
                case "routeHiboard" -> target.routeHiboard(true);
                case "routeRearScreen" -> target.routeRearScreen(true);
                case "guardSecondaryNavigationDecorated" ->
                        target.guardSecondaryNavigationDecorated(true);
                case "guardSecondaryNavigationSuppressed" ->
                        target.guardSecondaryNavigationSuppressed(true);
                case "guardHiboardTopGesture" -> target.guardHiboardTopGesture(true);
                case "guardStartupVolume" -> target.guardStartupVolume(true);
                case "guardInterguestStartup" -> target.guardInterguestStartup(true);
                case "guardKeyManager" -> target.guardKeyManager(true);
                case "guardSystemPolicyHardware" -> target.guardSystemPolicyHardware(true);
                case "guardKeyIngress" -> target.guardKeyIngress(true);
                case "guardNetwork" -> target.guardNetwork(true);
                case "guardPhoneProvider" -> target.guardPhoneProvider(true);
                case "guardAudioPolicy" -> target.guardAudioPolicy(true);
                case "guardAudioConsumer" -> target.guardAudioConsumer(true);
                case "guardUsbPlayer" -> target.guardUsbPlayer(true);
                case "guardMediaNotification" -> target.guardMediaNotification(true);
                case "guardPassionMcuProperty" -> target.guardPassionMcuProperty(true);
                case "guardPassionNavigationReady" -> target.guardPassionNavigationReady(true);
                case "guardPassionStatusReady" -> target.guardPassionStatusReady(true);
                case "guardPassionEcuHardware" -> target.guardPassionEcuHardware(true);
                case "guardPassionAvmNexus" -> target.guardPassionAvmNexus(true);
                case "guardPassionDisplayPower" -> target.guardPassionDisplayPower(true);
                case "guardPassionNexusPower" -> target.guardPassionNexusPower(true);
                case "guardStockStatusBar" -> target.guardStockStatusBar(true);
                case "guardPassionStatusBar" -> target.guardPassionStatusBar(true);
                case "guardPassionMusicDayDrawables" -> target.guardPassionMusicDayDrawables(true);
                case "guardPassionMusicForeground" -> target.guardPassionMusicForeground(true);
                case "guardPassionMusicInsets" -> target.guardPassionMusicInsets(true);
                case "guardPassionUsbMediaSession" -> target.guardPassionUsbMediaSession(true);
                case "guardPassionCarSettingsRecycler" ->
                        target.guardPassionCarSettingsRecycler(true);
                case "guardPassionSettingsBluetoothRole" ->
                        target.guardPassionSettingsBluetoothRole(true);
                case "guardPassionSettingsSound" -> target.guardPassionSettingsSound(true);
                case "guardPassionSettingsUnits" -> target.guardPassionSettingsUnits(true);
                case "guardPassionCameraVirtual" -> target.guardPassionCameraVirtual(true);
                case "guardPassionPhone" -> target.guardPassionPhone(true);
                case "guardPassionAmbientLight" -> target.guardPassionAmbientLight(true);
                case "guardPassionLauncherAudio" -> target.guardPassionLauncherAudio(true);
                case "guardPassionBluetoothVolume" -> target.guardPassionBluetoothVolume(true);
                case "guardPassionStartupVolume" -> target.guardPassionStartupVolume(true);
                case "guardPassionServiceForeground" -> target.guardPassionServiceForeground(true);
                case "guardPassionVehicleAnimation" -> target.guardPassionVehicleAnimation(true);
                case "guardPassionVehicleAudio" -> target.guardPassionVehicleAudio(true);
                case "guardPassionVehicleBluetooth" -> target.guardPassionVehicleBluetooth(true);
                case "guardPassionBluetoothRole" -> target.guardPassionBluetoothRole(true);
                case "guardPassionSidebars" -> target.guardPassionSidebars(true);
                case "guardPassionDiagnostics" -> target.guardPassionDiagnostics(true);
                case "guardCameraVirtual" -> target.guardCameraVirtual(true);
                case "guardOriginalNaviVin" -> target.guardOriginalNaviVin(true);
                case "guardOriginalNaviVehicle" -> target.guardOriginalNaviVehicle(true);
                case "guardOriginalNaviEgl" -> target.guardOriginalNaviEgl(true);
                case "guardOriginalNaviLibraries" -> target.guardOriginalNaviLibraries(true);
                case "guardNaviEgl" -> target.guardNaviEgl(true);
                case "guardNaviVin" -> target.guardNaviVin(true);
                case "guardNaviVehicle" -> target.guardNaviVehicle(true);
                case "guardNaviVersion" -> target.guardNaviVersion(true);
                case "guardMediaCloudAvailability" -> target.guardMediaCloudAvailability(true);
                case "guardMusicFullscreen" -> target.guardMusicFullscreen(true);
                case "guardGalleryArm64" -> target.guardGalleryArm64(true);
                case "guardMusicPage" -> target.guardMusicPage(true);
                case "guardAqyProvider" -> target.guardAqyProvider(true);
                case "guardLauncherDvr" -> target.guardLauncherDvr(true);
                case "guardLauncherCanbus" -> target.guardLauncherCanbus(true);
                case "guardLauncherPower" -> target.guardLauncherPower(true);
                case "guardSettingsVoiceClone" -> target.guardSettingsVoiceClone(true);
                case "guardSettingsVoiceMute" -> target.guardSettingsVoiceMute(true);
                case "guardStrPower" -> target.guardStrPower(true);
                default -> target.removePinnedEntry((String) args.get(0), (String) args.get(1));
            }
        }
    }

    private static void require(boolean condition, String message) {
        Recipes.require(condition, message);
    }
}
