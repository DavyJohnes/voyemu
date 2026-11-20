package com.voyemu.prepare;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.NoSuchAlgorithmException;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

/** Recipe target over one patched artifact step: DEX classes, APK entries and helper DEX. */
final class PatchTarget implements Recipes.Target {
    /** State shared by every step of one patched artifact. */
    record Patch(
            ProfileRegistry.Profile profile,
            int api,
            String abi,
            Toolchains tools,
            String type,
            String reference,
            Path input,
            Path dir,
            Path decoded,
            Map<String, Path> uses,
            Map<String, byte[]> entries,
            DexClasses dex,
            Map<String, Set<String>> classSets,
            List<Path> added,
            Map<String, byte[]> donorSlots) {}

    private final ProfileRegistry.Profile profile;
    private final int api;
    private final String abi;
    private final Toolchains tools;
    private final String type, reference;
    private final Path input, dir, decoded, stepRoot;
    private final Map<String, Path> uses;
    private final Map<String, byte[]> entries;
    private final DexClasses dex;
    private final Map<String, Set<String>> classSets;
    private final List<Path> added;
    private final Map<String, byte[]> donorSlots;
    private final Map<String, Object> constants;
    private final boolean planValidation;

    PatchTarget(Patch patch, Path stepRoot, Map<String, Object> constants, boolean planValidation) {
        profile = patch.profile();
        api = patch.api();
        abi = patch.abi();
        tools = patch.tools();
        type = patch.type();
        reference = patch.reference();
        input = patch.input();
        dir = patch.dir();
        decoded = patch.decoded();
        uses = patch.uses();
        entries = patch.entries();
        dex = patch.dex();
        classSets = patch.classSets();
        added = patch.added();
        donorSlots = patch.donorSlots();
        this.stepRoot = stepRoot;
        this.constants = Map.copyOf(constants);
        this.planValidation = planValidation;
    }

    @Override
    public String profile() {
        return profile.referenceProfile();
    }

    @Override
    public Map<String, Object> constants() {
        return constants;
    }

    @Override
    public int api() {
        return api;
    }

    @Override
    public String abi() {
        return abi;
    }

    @Override
    public Path work() {
        return decoded;
    }

    @Override
    public DexClasses dex() {
        Failure.require(
                dex != null, "PATCH_OPERATION_UNSUPPORTED", "Method edits require API28 DEX", 5);

        return dex;
    }

    @Override
    public Path output() {
        return type.equals("file") ? decoded.resolve("output") : null;
    }

    @Override
    public Map<String, Path> uses() {
        return uses;
    }

    @Override
    public Path stepRoot() {
        return stepRoot;
    }

    @Override
    public String originalSha256() {
        try {
            return FilesSupport.hash(input);
        } catch (IOException | NoSuchAlgorithmException error) {
            throw new Failure("PATCH_INPUT", "Could not verify original artifact", 5);
        }
    }

    @Override
    public boolean derivedInput() {
        return reference.startsWith("component:") || reference.startsWith("patched:");
    }

    @Override
    public void addDex(String relative) throws Exception {
        Path source = Storage.confined(stepRoot, relative);
        Path output =
                GuestJava.dex(
                        new GuestJava.Unit(source, api),
                        tools.platform(),
                        dir.resolve("helper-" + added.size()));

        added.addAll(FilesSupport.tree(output).values());
    }

    @Override
    public void putEntry(Path source, String name) throws Exception {
        NativeCore.safeEntry(name);
        Failure.require(
                !name.endsWith(".dex")
                        && !name.equals("AndroidManifest.xml")
                        && !name.equals("resources.arsc")
                        && !name.startsWith("res/")
                        && !DexOperations.signatureEntry(name),
                "PATCH_ENTRY_FORBIDDEN",
                "Protected APK entry",
                5);

        if (name.startsWith("lib/")) {
            Artifacts.elf(source, abi);
        } else {
            Failure.require(
                    name.equals("assets/voyemu-camera-capabilities.json")
                            && Files.size(source) <= 4096,
                    "PATCH_ENTRY_FORBIDDEN",
                    "Only the registered camera contract asset is" + " supported",
                    5);
        }

        entries.put(name, Files.readAllBytes(source));
    }

    @Override
    public void verifyPinnedEntry(String name, String pin) throws Exception {
        Artifacts.verifyPinnedNativeEntry(entries, name, pin);
    }

    @Override
    public void removeEntry(String name) {
        Failure.require(
                name.startsWith("lib/") && entries.containsKey(name),
                "PATCH_ENTRY_FORBIDDEN",
                "Expected removable native entry",
                5);
        entries.remove(name);
    }

    @Override
    public void addNxpPhoneProvider() throws Exception {
        Failure.require(
                api == 28,
                "PATCH_PROFILE_UNSUPPORTED",
                "NXP Phone provider requires Android API 28",
                5);
        entries.put("AndroidManifest.xml", PhoneManifest.add(entries.get("AndroidManifest.xml")));
        addDex("nxp-phone-provider/src");
    }

    @Override
    public void addPhoneProvider() throws Exception {
        Failure.require(
                api == 28,
                "PATCH_PROFILE_UNSUPPORTED",
                "A9 Phone provider requires Android API 28",
                5);
        entries.put(
                "classes.dex",
                Api28PhoneCallWindowGuard.apply(
                        profile.referenceProfile(), api, input, entries.get("classes.dex")));
        entries.put("AndroidManifest.xml", PhoneManifest.add(entries.get("AndroidManifest.xml")));
        addDex("phone-provider/src");
    }

    @Override
    public void useLibraries(List<String> names) throws Exception {
        entries.put(
                "AndroidManifest.xml",
                ManifestLibraries.add(entries.get("AndroidManifest.xml"), names));
    }

    @Override
    public void guardPhoneProvider(boolean apply) throws Exception {
        Path source = PhoneProviderGuard.source(profile, constants);
        byte[] manifest =
                PhoneProviderGuard.manifest(
                        constants, api, abi, input, entries.get("AndroidManifest.xml"));

        if (!apply) {
            PhoneCallWindowGuard.validateOriginal(
                    constants, api, input, entries.get("classes.dex"));
        }

        if (apply) {
            byte[] window =
                    PhoneCallWindowGuard.apply(constants, api, input, entries.get("classes.dex"));
            entries.put("classes.dex", PhoneDisconnectGuard.apply(constants, api, input, window));
            entries.put("AndroidManifest.xml", manifest);
            Path dex =
                    PhoneProviderGuard.compile(
                            source, tools.platform(), dir.resolve("phone-provider-helper"));
            added.addAll(FilesSupport.tree(dex).values());
        }
    }

    @Override
    public void guardAudioPolicy(boolean apply) throws Exception {
        Path helper = AudioPolicyGuard.helperSource();

        AudioPolicyGuard.validateHelperSources(helper);
        AudioPolicyGuard.require(entries.containsKey("lib/armeabi/libqg_audio_jni.so"));

        if (apply) {
            entries.put(
                    "classes.dex",
                    AudioPolicyGuard.apply(
                            profile.referenceProfile(),
                            api,
                            abi,
                            input,
                            entries.get("classes.dex")));
            entries.remove("lib/armeabi/libqg_audio_jni.so");
            Path output =
                    GuestJava.dex(
                            new GuestJava.Unit(helper, api),
                            tools.platform(),
                            dir.resolve("audio-helper-java"));
            added.addAll(FilesSupport.tree(output).values());
        } else {
            AudioPolicyGuard.validate(
                    profile.referenceProfile(), api, abi, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void guardAudioConsumer(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes.dex",
                    AudioPolicyConsumerGuard.apply(
                            profile.referenceProfile(),
                            api,
                            abi,
                            input,
                            entries.get("classes.dex")));
        } else {
            AudioPolicyConsumerGuard.validate(
                    profile.referenceProfile(), api, abi, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void guardMediaNotification(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes2.dex",
                    MediaNotificationGuard.apply(
                            constants, api, abi, input, entries.get("classes2.dex")));
        } else {
            MediaNotificationGuard.validate(
                    constants, api, abi, input, entries.get("classes2.dex"));
        }
    }

    @Override
    public void guardCameraVirtual(boolean apply) throws Exception {
        Path source = Storage.confined(stepRoot, CameraVirtualGuard.HELPER_ROOT);
        var files = FilesSupport.tree(source);

        Failure.require(
                files.keySet().equals(Set.of("com/voyemu/camera/BackCameraProbe.java")),
                "CAMERA_VIRTUAL",
                "Exact Camera helper source required",
                3);

        if (apply) {
            entries.put(
                    "classes.dex",
                    CameraVirtualGuard.apply(
                            constants, api, abi, input, entries.get("classes.dex")));
            addDex(CameraVirtualGuard.HELPER_ROOT);
        } else {
            CameraVirtualGuard.validate(constants, api, abi, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void guardOriginalNaviVin(boolean apply) throws Exception {
        String root = "api30-original-navi-identity/vin-src",
                relative = "com/voyemu/qgapi/BrokerVin.java";
        Path source = Storage.confined(stepRoot, root);
        var files = FilesSupport.tree(source);

        Failure.require(
                files.keySet().equals(Set.of(relative)),
                "ORIGINAL_NAVI_VIN",
                "Exact helper required",
                3);

        if (apply) {
            byte[] result =
                    OriginalNaviVinGuard.apply(
                            profile.referenceProfile(),
                            api,
                            abi,
                            input,
                            entries.get("classes.dex"));
            entries.put("classes.dex", result);
            addDex(root);
        } else {
            OriginalNaviVinGuard.validateInput(
                    profile.referenceProfile(), api, abi, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void guardOriginalNaviVehicle(boolean apply) throws Exception {
        String root = "api30-original-navi-identity/vehicle-src",
                relative = "com/voyemu/qgapi/VehicleBroker.java";
        Path source = Storage.confined(stepRoot, root);
        var files = FilesSupport.tree(source);

        Failure.require(
                files.keySet().equals(Set.of(relative)),
                "ORIGINAL_NAVI_VEHICLE",
                "Exact helper required",
                3);
        byte[] result =
                OriginalNaviVehicleGuard.apply(
                        profile.referenceProfile(), api, abi, input, entries.get("classes.dex"));

        if (apply) {
            entries.put("classes.dex", result);
            addDex(root);
        }
    }

    @Override
    public void guardOriginalNaviEgl(boolean apply) throws Exception {
        Path source = Storage.confined(stepRoot, "original-navi-egl/src");
        var files = FilesSupport.tree(source);

        Failure.require(
                files.keySet()
                                .equals(
                                        Set.of(
                                                "com/voyemu/egl/ScopedEgl.java",
                                                "com/voyemu/egl/ScopedEglHook.java",
                                                "com/voyemu/navi/OwnContext.java"))
                        && abi.equals("arm64-v8a"),
                "ORIGINAL_NAVI_EGL",
                "Exact helper and ABI required",
                3);
        byte[] result =
                OriginalNaviEglGuard.apply(
                        profile.referenceProfile(), api, entries.get("classes4.dex"));

        if (apply) {
            entries.put("classes4.dex", result);
            addDex("original-navi-egl/src");
        }
    }

    @Override
    public void guardOriginalNaviLibraries(boolean apply) throws Exception {
        Path source = Storage.confined(stepRoot, "original-navi-libraries/src");
        var files = FilesSupport.tree(source);

        Failure.require(
                files.keySet()
                                .equals(
                                        Set.of(
                                                "com/android/server/pm/parsing/library/QgLibraries.java",
                                                "compile-only/com/android/server/pm/parsing/pkg/ParsedPackage.java",
                                                "compile-only/com/android/server/pm/parsing/library/PackageSharedLibraryUpdater.java"))
                        && abi.equals("arm64-v8a"),
                "ORIGINAL_NAVI_LIBRARIES",
                "Exact helper and ABI required",
                3);
        byte[] result =
                OriginalNaviLibrariesGuard.apply(
                        profile.referenceProfile(), api, entries.get("classes.dex"));

        if (apply) {
            entries.put("classes.dex", result);
            Path helper =
                    GuestJava.dex(
                            new GuestJava.Unit(source, api),
                            tools.platform(),
                            dir.resolve("helper-" + added.size()));
            added.addAll(FilesSupport.tree(helper).values());
        }
    }

    @Override
    public void guardNaviEgl(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes.dex",
                    NaviEglGuard.apply(
                            profile.referenceProfile(),
                            api,
                            abi,
                            input,
                            entries.get("classes.dex")));
        } else {
            NaviEglGuard.validate(
                    profile.referenceProfile(), api, abi, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void guardNaviVin(boolean apply) throws Exception {
        Path source = Storage.confined(stepRoot, NaviVinGuard.HELPER_ROOT);
        var files = FilesSupport.tree(source);

        Failure.require(
                files.keySet().equals(Set.of("com/voyemu/navi/GenuineVin.java")),
                "NAVI_VIN",
                "Exact genuine VIN helper source required",
                3);

        if (apply) {
            entries.put(
                    "classes3.dex",
                    NaviVinGuard.apply(
                            profile.referenceProfile(),
                            api,
                            abi,
                            input,
                            entries.get("classes3.dex")));
            addDex(NaviVinGuard.HELPER_ROOT);
        } else {
            NaviVinGuard.validate(
                    profile.referenceProfile(), api, abi, input, entries.get("classes3.dex"));
        }
    }

    @Override
    public void guardNaviVehicle(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes3.dex",
                    NaviVehicleGuard.apply(
                            profile.referenceProfile(),
                            api,
                            abi,
                            input,
                            entries.get("classes3.dex")));
        } else {
            NaviVehicleGuard.validate(
                    profile.referenceProfile(), api, abi, input, entries.get("classes3.dex"));
        }
    }

    @Override
    public void guardNaviVersion(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes3.dex",
                    NaviVersionGuard.apply(
                            profile.referenceProfile(),
                            api,
                            abi,
                            input,
                            entries.get("classes3.dex")));
        } else {
            NaviVersionGuard.validate(
                    profile.referenceProfile(), api, abi, input, entries.get("classes3.dex"));
        }
    }

    @Override
    public void guardMediaCloudAvailability(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "AndroidManifest.xml",
                    MediaCloudManifestGuard.apply(
                            constants, api, abi, input, entries.get("AndroidManifest.xml")));
            entries.put(
                    "classes3.dex",
                    MediaCloudConfigGuard.apply(
                            constants, api, abi, input, entries.get("classes3.dex")));
        } else {
            MediaCloudManifestGuard.validate(
                    constants, api, abi, input, entries.get("AndroidManifest.xml"));
            MediaCloudConfigGuard.validate(constants, api, abi, input, entries.get("classes3.dex"));
        }
    }

    @Override
    public void guardApi28UsbPlayer() throws Exception {
        Path source = Storage.confined(stepRoot, "api28-usb-player/src");

        Api28UsbPlayerGuard.validateSource(source);
        Api28UsbPlayerGuard.adaptClasses(profile.referenceProfile(), api, abi, input, dex());
        added.add(
                Api28UsbPlayerGuard.compileHelper(
                        profile.referenceProfile(),
                        api,
                        abi,
                        input,
                        source,
                        tools.platform(),
                        dir.resolve("helper-" + added.size())));
    }

    @Override
    public void guardUsbPlayer(boolean apply) throws Exception {
        Path source = Storage.confined(stepRoot, "api30-usb-player/src");

        UsbPlayerGuard.validateSource(source);

        if (apply) {
            entries.put(
                    "classes3.dex",
                    UsbPlayerGuard.apply(constants, api, abi, input, entries.get("classes3.dex")));
            added.add(
                    UsbPlayerGuard.compileHelper(
                            constants,
                            api,
                            abi,
                            input,
                            source,
                            tools.platform(),
                            dir.resolve("helper-" + added.size())));
        } else if (planValidation) {
            UsbPlayerGuard.validateOriginal(
                    constants, api, abi, input, entries.get("classes3.dex"));
        } else {
            UsbPlayerGuard.validatePrepared(
                    constants, api, abi, input, entries.get("classes3.dex"));
        }
    }

    @Override
    public void guardMusicFullscreen(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes.dex",
                    MusicFullscreenGuard.apply(constants, api, input, entries.get("classes.dex")));
        } else if (planValidation) {
            MusicFullscreenGuard.validateOriginalChain(
                    constants, api, input, entries.get("classes.dex"));
        } else {
            MusicFullscreenGuard.validate(constants, api, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void guardGalleryArm64(boolean apply) throws Exception {
        Path component = uses.get("component:gallery-arm64-libraries");

        Failure.require(
                component != null,
                "PATCH_DEPENDENCY_MISSING",
                "Gallery archive component required",
                3);
        var next =
                GalleryArm64Libraries.add(
                        constants,
                        api,
                        abi,
                        input,
                        entries,
                        component,
                        component.getParent().resolve("source"));

        if (apply) {
            entries.clear();
            entries.putAll(next);
        }
    }

    @Override
    public void guardMusicPage(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes.dex",
                    MusicPageGuard.apply(constants, api, input, entries.get("classes.dex")));
        } else {
            MusicPageGuard.validate(constants, api, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void guardAqyProvider(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes2.dex",
                    AqyProviderGuard.apply(constants, api, input, entries.get("classes2.dex")));
        } else {
            AqyProviderGuard.validateOriginal(constants, api, input);
        }
    }

    @Override
    public void guardLauncherDvr(boolean apply) throws Exception {
        Path source = Storage.confined(stepRoot, LauncherDvrGuard.HELPER_ROOT);
        var files = FilesSupport.tree(source);

        Failure.require(
                files.keySet().equals(Set.of("com/voyemu/launcher/DvrAvailability.java")),
                "LAUNCHER_DVR",
                "Exact Launcher DVR helper source required",
                3);

        if (apply) {
            entries.put(
                    "classes2.dex",
                    LauncherDvrGuard.apply(
                            constants, api, abi, input, entries.get("classes2.dex")));
            addDex(LauncherDvrGuard.HELPER_ROOT);
        } else if (planValidation) {
            LauncherDvrGuard.validateOriginalChain(
                    constants, api, abi, input, entries.get("classes2.dex"));
        } else {
            LauncherDvrGuard.validate(constants, api, abi, input, entries.get("classes2.dex"));
        }
    }

    @Override
    public void guardLauncherCanbus(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes2.dex",
                    LauncherCanbusGuard.apply(
                            constants, api, abi, input, entries.get("classes2.dex")));
        } else {
            LauncherCanbusGuard.validate(constants, api, abi, input, entries.get("classes2.dex"));
        }
    }

    @Override
    public void guardLauncherPower(boolean apply) throws Exception {
        for (String entry : List.of("classes.dex", "classes2.dex")) {
            if (apply) {
                entries.put(
                        entry,
                        LauncherPowerGuard.apply(constants, api, input, entries.get(entry), entry));
            } else {
                LauncherPowerGuard.validate(constants, api, input, entries.get(entry), entry);
            }
        }
    }

    @Override
    public void guardSettingsVoiceMute(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes.dex",
                    SettingsVoiceMuteGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes.dex")));
        } else if (planValidation) {
            SettingsVoiceMuteGuard.validateOriginalChain(
                    profile.referenceProfile(), api, input, entries.get("classes.dex"));
        } else {
            SettingsVoiceMuteGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void guardSettingsVoiceClone(boolean apply) throws Exception {
        boolean soundBalance = SettingsVoiceCloneGuard.soundBalance(profile.referenceProfile());

        if (apply) {
            byte[] dex =
                    SettingsVoiceCloneGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes.dex"));

            if (!soundBalance) {
                entries.put("classes.dex", dex);

                return;
            }

            entries.put(
                    "classes.dex", SoundBalanceGuard.apply(profile.referenceProfile(), api, dex));
            entries.put(
                    "AndroidManifest.xml",
                    SettingsManifest.apply(
                            profile.referenceProfile(),
                            api,
                            input,
                            entries.get("AndroidManifest.xml")));
        } else {
            SettingsVoiceCloneGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes.dex"));

            if (!soundBalance) {
                return;
            }

            SoundBalanceGuard.validate(profile.referenceProfile(), api, entries.get("classes.dex"));

            if (planValidation) {
                SettingsManifest.validateOriginal(
                        profile.referenceProfile(), api, input, entries.get("AndroidManifest.xml"));
            } else {
                SettingsManifest.validate(
                        profile.referenceProfile(), api, input, entries.get("AndroidManifest.xml"));
            }
        }
    }

    @Override
    public void guardStrPower(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes2.dex",
                    StrPowerGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes2.dex")));
        } else {
            StrPowerGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes2.dex"));
        }
    }

    @Override
    public void guardPassionMcuProperty(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes16.dex",
                    PassionMcuPropertyGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes16.dex")));
            entries.put(
                    "AndroidManifest.xml",
                    PassionMcuManifest.add(entries.get("AndroidManifest.xml")));
            added.add(
                    PassionMcuPropertyGuard.compileHelper(
                            profile.referenceProfile(),
                            api,
                            input,
                            Storage.confined(stepRoot, "passion-api30-mcu-property/src"),
                            tools.platform(),
                            dir.resolve("helper-" + added.size())));
        } else {
            PassionMcuPropertyGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes16.dex"));
            PassionMcuManifest.add(entries.get("AndroidManifest.xml"));
        }
    }

    @Override
    public void guardPassionNavigationReady(boolean apply) throws Exception {
        PassionNavigationReadyGuard.validateHelperSource(
                Storage.confined(stepRoot, "passion-api30-navigation-ready/src"));

        if (apply) {
            entries.put(
                    "classes3.dex",
                    PassionNavigationReadyGuard.applyPrepared(
                            profile.referenceProfile(), api, input, entries.get("classes3.dex")));
            addDex("passion-api30-navigation-ready/src");
        } else if (planValidation) {
            PassionNavigationReadyGuard.validateOriginalChain(
                    profile.referenceProfile(), api, input, entries.get("classes3.dex"));
        } else {
            PassionNavigationReadyGuard.validatePrepared(
                    profile.referenceProfile(), api, input, entries.get("classes3.dex"));
        }
    }

    @Override
    public void guardPassionStatusReady(boolean apply) throws Exception {
        PassionStatusReadyGuard.validateHelperSource(
                Storage.confined(stepRoot, "passion-api30-status-ready/src"));

        if (apply) {
            entries.put(
                    "classes3.dex",
                    PassionStatusReadyGuard.applyPrepared(
                            profile.referenceProfile(), api, input, entries.get("classes3.dex")));
            addDex("passion-api30-status-ready/src");
        } else if (planValidation) {
            PassionStatusReadyGuard.validateOriginalChain(
                    profile.referenceProfile(), api, input, entries.get("classes3.dex"));
        } else {
            PassionStatusReadyGuard.validatePrepared(
                    profile.referenceProfile(), api, input, entries.get("classes3.dex"));
        }
    }

    @Override
    public void guardPassionEcuHardware(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes2.dex",
                    PassionGuestNullGuards.applyPrepared(
                            profile.referenceProfile(),
                            api,
                            input,
                            entries.get("classes2.dex"),
                            PassionGuestNullGuards.ECU));
        } else if (planValidation) {
            PassionGuestNullGuards.validateOriginalChain(
                    profile.referenceProfile(),
                    api,
                    input,
                    entries.get("classes2.dex"),
                    PassionGuestNullGuards.ECU);
        } else {
            PassionGuestNullGuards.validatePrepared(
                    profile.referenceProfile(),
                    api,
                    input,
                    entries.get("classes2.dex"),
                    PassionGuestNullGuards.ECU);
        }
    }

    @Override
    public void guardPassionAvmNexus(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes2.dex",
                    PassionGuestNullGuards.applyPrepared(
                            profile.referenceProfile(),
                            api,
                            input,
                            entries.get("classes2.dex"),
                            PassionGuestNullGuards.AVM));
        } else if (planValidation) {
            PassionGuestNullGuards.validateOriginalChain(
                    profile.referenceProfile(),
                    api,
                    input,
                    entries.get("classes2.dex"),
                    PassionGuestNullGuards.AVM);
        } else {
            PassionGuestNullGuards.validatePrepared(
                    profile.referenceProfile(),
                    api,
                    input,
                    entries.get("classes2.dex"),
                    PassionGuestNullGuards.AVM);
        }
    }

    @Override
    public void guardPassionDisplayPower(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes.dex",
                    PassionDisplayPowerGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes.dex")));
            addDex("passion-api30-display-power/src");
        } else {
            PassionDisplayPowerGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void guardPassionNexusPower(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes.dex",
                    PassionNexusPowerGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes.dex")));
            addDex("passion-api30-nexus-power/src");
        } else {
            PassionNexusPowerGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void guardStockStatusBar(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes.dex",
                    StockStatusBarGuard.apply(constants, api, input, entries.get("classes.dex")));
            addDex("api30-stock-statusbar/src");
        } else {
            StockStatusBarGuard.validate(constants, api, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void guardPassionStatusBar(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes.dex",
                    PassionStatusBarGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes.dex")));
            addDex("passion-api30-statusbar/src");
        } else {
            PassionStatusBarGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void guardPassionMusicDayDrawables(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "resources.arsc",
                    PassionMusicDayDrawableGuard.apply(
                            profile.referenceProfile(), api, input, entries));
        } else {
            PassionMusicDayDrawableGuard.validate(profile.referenceProfile(), api, input, entries);
        }
    }

    @Override
    public void guardPassionMusicInsets(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes2.dex",
                    PassionMusicInsetsGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes2.dex")));
        } else if (planValidation) {
            PassionMusicInsetsGuard.validateOriginalChain(
                    profile.referenceProfile(), api, input, entries.get("classes2.dex"));
        } else {
            PassionMusicInsetsGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes2.dex"));
        }
    }

    @Override
    public void guardPassionUsbMediaSession(boolean apply) throws Exception {
        Path source = Storage.confined(stepRoot, "passion-api30-usb-media-session/src");

        PassionUsbMediaSessionGuard.validateSource(source);

        if (apply) {
            entries.put(
                    "classes2.dex",
                    PassionUsbMediaSessionGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes2.dex")));
            added.add(
                    PassionUsbMediaSessionGuard.compileHelper(
                            profile.referenceProfile(),
                            api,
                            input,
                            source,
                            tools.platform(),
                            dir.resolve("helper-" + added.size())));
        } else if (planValidation) {
            PassionUsbMediaSessionGuard.validateOriginalChain(
                    profile.referenceProfile(), api, input, entries.get("classes2.dex"));
        } else {
            PassionUsbMediaSessionGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes2.dex"));
        }
    }

    @Override
    public void guardPassionCarSettingsRecycler(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes4.dex",
                    PassionCarSettingsRecyclerGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes4.dex")));
        } else {
            PassionCarSettingsRecyclerGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes4.dex"));
        }
    }

    @Override
    public void guardPassionSettingsBluetoothRole(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes6.dex",
                    PassionSettingsBluetoothRoleGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes6.dex")));
        } else {
            PassionSettingsBluetoothRoleGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes6.dex"));
        }
    }

    @Override
    public void guardPassionCameraVirtual(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes2.dex",
                    PassionCameraVirtualGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes2.dex")));
        } else if (planValidation) {
            PassionCameraVirtualGuard.validateOriginalChain(
                    profile.referenceProfile(), api, input, entries.get("classes2.dex"));
        } else {
            PassionCameraVirtualGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes2.dex"));
        }
    }

    @Override
    public void guardPassionPhone(boolean apply) throws Exception {
        PassionPhoneGuard.validateHelperSource(
                Storage.confined(stepRoot, PassionPhoneGuard.HELPER_ROOT));
        // The plan pass reads the original manifest. The apply pass has run
        // useLibraries.
        byte[] manifest =
                PassionPhoneGuard.dialerManifest(
                        entries.get("AndroidManifest.xml"), !planValidation);

        if (apply) {
            entries.put(
                    "classes2.dex",
                    PassionPhoneGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes2.dex")));
            entries.put("AndroidManifest.xml", manifest);
            addDex(PassionPhoneGuard.HELPER_ROOT);
        } else {
            PassionPhoneGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes2.dex"));
        }
    }

    @Override
    public void guardPassionSettingsUnits(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes5.dex",
                    PassionSettingsUnitsGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes5.dex")));
        } else if (planValidation) {
            PassionSettingsUnitsGuard.validateOriginalChain(
                    profile.referenceProfile(), api, input, entries.get("classes5.dex"));
        } else {
            PassionSettingsUnitsGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes5.dex"));
        }
    }

    @Override
    public void guardPassionSettingsSound(boolean apply) throws Exception {
        Path soundSource = Storage.confined(stepRoot, "passion-api30-settings-sound/src");

        PassionSettingsSoundGuard.validateSource(soundSource);

        if (apply) {
            entries.put(
                    "classes5.dex",
                    PassionSettingsSoundGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes5.dex")));
            added.add(
                    PassionSettingsSoundGuard.compileHelper(
                            profile.referenceProfile(),
                            api,
                            input,
                            soundSource,
                            tools.platform(),
                            dir.resolve("helper-" + added.size())));
        } else {
            PassionSettingsSoundGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes5.dex"));
        }
    }

    @Override
    public void guardPassionMusicForeground(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes2.dex",
                    PassionMusicForegroundGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes2.dex")));
        } else {
            PassionMusicForegroundGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes2.dex"));
        }
    }

    @Override
    public void guardPassionAmbientLight(boolean apply) throws Exception {
        for (String dex : List.of("classes2.dex", "classes3.dex")) {
            if (apply) {
                entries.put(
                        dex,
                        PassionAmbientLightGuard.apply(
                                profile.referenceProfile(), api, input, entries.get(dex)));
            } else if (planValidation) {
                PassionAmbientLightGuard.validateOriginalChain(
                        profile.referenceProfile(), api, input, dex, entries.get(dex));
            } else {
                PassionAmbientLightGuard.validate(
                        profile.referenceProfile(), api, input, entries.get(dex));
            }
        }
    }

    @Override
    public void guardPassionLauncherAudio(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes3.dex",
                    PassionLauncherAudioGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes3.dex")));
        } else if (planValidation) {
            PassionLauncherAudioGuard.validateOriginalChain(
                    profile.referenceProfile(), api, input, entries.get("classes3.dex"));
        } else {
            PassionLauncherAudioGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes3.dex"));
        }
    }

    @Override
    public void guardPassionBluetoothVolume(boolean apply) throws Exception {
        String dex =
                FilesSupport.hash(input).equals(PassionServiceForegroundGuard.VIDEO)
                        ? "classes2.dex"
                        : "classes3.dex";

        if (apply) {
            entries.put(
                    dex,
                    PassionBluetoothVolumeGuard.apply(
                            profile.referenceProfile(), api, input, entries.get(dex)));
        } else if (planValidation) {
            PassionBluetoothVolumeGuard.validateOriginalChain(
                    profile.referenceProfile(), api, input, entries.get(dex));
        } else {
            PassionBluetoothVolumeGuard.validate(
                    profile.referenceProfile(), api, input, entries.get(dex));
        }
    }

    @Override
    public void guardPassionStartupVolume(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes.dex",
                    PassionStartupVolumeGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes.dex")));
        } else if (planValidation) {
            PassionStartupVolumeGuard.validateOriginalChain(
                    profile.referenceProfile(), api, input, entries.get("classes.dex"));
        } else {
            PassionStartupVolumeGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void guardPassionServiceForeground(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes2.dex",
                    PassionServiceForegroundGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes2.dex")));
        } else {
            PassionServiceForegroundGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes2.dex"));
        }
    }

    @Override
    public void guardPassionVehicleAnimation(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes3.dex",
                    PassionVehicleAnimationGuard.applyPrepared(
                            profile.referenceProfile(), api, input, entries.get("classes3.dex")));
        } else if (planValidation) {
            PassionVehicleAnimationGuard.validateOriginalChain(
                    profile.referenceProfile(), api, input, entries.get("classes3.dex"));
        } else {
            PassionVehicleAnimationGuard.validatePrepared(
                    profile.referenceProfile(), api, input, entries.get("classes3.dex"));
        }
    }

    @Override
    public void guardPassionVehicleAudio(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes3.dex",
                    PassionVehicleAudioGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes3.dex")));
        } else {
            PassionVehicleAudioGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes3.dex"));
        }
    }

    @Override
    public void guardPassionVehicleBluetooth(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes2.dex",
                    PassionVehicleBluetoothGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes2.dex")));
            entries.put(
                    "classes.dex",
                    PassionVehicleBluetoothGuard.applySettings(
                            profile.referenceProfile(), api, input, entries.get("classes.dex")));
        } else {
            PassionVehicleBluetoothGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes2.dex"));
            PassionVehicleBluetoothGuard.validateSettings(
                    profile.referenceProfile(), api, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void guardPassionBluetoothRole(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes.dex",
                    PassionBluetoothRoleGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes.dex")));
        } else {
            PassionBluetoothRoleGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void guardPassionSidebars(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes3.dex",
                    PassionSidebarGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes3.dex")));
            addDex("passion-api30-sidebars/src");
        } else {
            PassionSidebarGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes3.dex"));
        }
    }

    @Override
    public void guardPassionDiagnostics(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes15.dex",
                    PassionDiagnosticsGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes15.dex")));
        } else {
            PassionDiagnosticsGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes15.dex"));
        }
    }

    @Override
    public void routeSystemUi(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes.dex",
                    SystemUiRouting.apply(constants, api, input, entries.get("classes.dex")));
            addDex("systemui-routing/src");
        } else {
            SystemUiRouting.validate(constants, api, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void guardHiboardStartup(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes.dex",
                    HiboardStartupGuard.apply(constants, api, input, entries.get("classes.dex")));
        } else if (planValidation) {
            HiboardStartupGuard.validateOriginalChain(
                    constants, api, input, entries.get("classes.dex"));
        } else {
            HiboardStartupGuard.validateRouted(constants, api, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void routeHiboard(boolean apply) throws Exception {
        var files = FilesSupport.tree(Storage.confined(stepRoot, "api30-hiboard-routing/src"));

        Failure.require(
                files.keySet().equals(Set.of("com/voyemu/output/HiboardRouter.java")),
                "HIBOARD_ROUTING",
                "Exact Hiboard helper required",
                3);

        if (apply) {
            entries.put(
                    "classes.dex",
                    HiboardRouting.apply(constants, api, input, entries.get("classes.dex")));
            addDex("api30-hiboard-routing/src");
        } else {
            HiboardRouting.validate(constants, api, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void routeRearScreen(boolean apply) throws Exception {
        var files = FilesSupport.tree(Storage.confined(stepRoot, "api30-thirdscreen-rear/src"));

        Failure.require(
                files.keySet()
                        .equals(
                                Set.of(
                                        "com/voyemu/output/RearScreen.java",
                                        "compile-only/android/app/ActivityThread.java",
                                        "compile-only/android/app/IOnTouchListener.java",
                                        "compile-only/android/view/InputChannel.java",
                                        "compile-only/android/view/InputEventReceiver.java",
                                        "compile-only/android/view/InputMonitor.java")),
                "THIRDSCREEN_ROUTING",
                "Exact ThirdScreen helper required",
                3);

        if (apply) {
            entries.put(
                    ThirdScreenRouting.DEX,
                    ThirdScreenRouting.apply(
                            constants, api, input, entries.get(ThirdScreenRouting.DEX)));
            addDex("api30-thirdscreen-rear/src");
        } else {
            ThirdScreenRouting.validate(constants, api, input, entries.get(ThirdScreenRouting.DEX));
        }
    }

    @Override
    public void guardSecondaryNavigationDecorated(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes2.dex",
                    PassionSecondaryNavigationGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes2.dex")));
        } else {
            PassionSecondaryNavigationGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes2.dex"));
        }
    }

    @Override
    public void guardSecondaryNavigationSuppressed(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes2.dex",
                    SecondaryNavigationGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes2.dex")));
        } else {
            SecondaryNavigationGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes2.dex"));
        }
    }

    @Override
    public void guardHiboardTopGesture(boolean apply) throws Exception {
        Path source = Storage.confined(stepRoot, "api30-hiboard-top-gesture");

        if (apply) {
            byte[] helper =
                    HiboardTopGestureGuard.buildHelper(
                            profile.referenceProfile(),
                            api,
                            input,
                            source,
                            tools.platform(),
                            dir.resolve("hiboard-top-gesture"));
            entries.put(
                    "classes2.dex",
                    HiboardTopGestureGuard.apply(
                            profile.referenceProfile(),
                            api,
                            input,
                            entries.get("classes2.dex"),
                            helper));
        } else {
            HiboardTopGestureGuard.validateOriginal(profile.referenceProfile(), api, input, source);
        }
    }

    @Override
    public void guardStartupVolume(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes.dex",
                    StartupVolumeGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes.dex")));
        } else {
            StartupVolumeGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void guardKeyManager(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes.dex",
                    KeyManagerGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes.dex")));
        } else {
            KeyManagerGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void guardSystemPolicyHardware(boolean apply) throws Exception {
        List<String> names =
                entries.keySet().stream()
                        .filter(entry -> entry.matches("classes(?:[2-9]|[1-9][0-9]+)?\\.dex"))
                        .sorted()
                        .toList();
        DexClasses classes = new DexClasses(entries, names, api);

        SystemPolicyHardwareGuard.apply(profile.referenceProfile(), api, originalSha256(), classes);

        if (apply) {
            classes.write(entries);
        }
    }

    @Override
    public void guardKeyIngress(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes.dex",
                    KeyManagerIngressGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes.dex")));
        } else {
            KeyManagerIngressGuard.validateOriginal(profile.referenceProfile(), api, input);
        }
    }

    @Override
    public void guardNetwork(boolean apply) throws Exception {
        if (apply) {
            entries.put(
                    "classes.dex",
                    NetworkGuard.apply(
                            profile.referenceProfile(), api, input, entries.get("classes.dex")));
        } else {
            NetworkGuard.validate(
                    profile.referenceProfile(), api, input, entries.get("classes.dex"));
        }
    }

    @Override
    public void useServices(List<String> names) throws Exception {
        entries.put(
                "AndroidManifest.xml",
                ManifestLibraries.addServices(entries.get("AndroidManifest.xml"), names));
    }

    @Override
    public void mergeClasses(String reference, Map<String, Object> pins) throws Exception {
        Path adapter = uses.get(reference);

        Failure.require(
                adapter != null
                        && Set.of("component:media", "component:media-callbacks")
                                .contains(reference),
                "PATCH_DEPENDENCY_MISSING",
                "Registered donor adapter is missing",
                5);
        Artifacts.mergeAdapterClasses(dex(), adapter, pins, classSets, api);
    }

    @Override
    public void donorSlot(String reference, Map<String, Object> pins) throws Exception {
        Path adapter = uses.get(reference);

        Failure.require(
                adapter != null
                        && Set.of(
                                        "component:media-callbacks",
                                        "component:settings-runtime",
                                        "component:settings-managers",
                                        "component:systemui-runtime-a",
                                        "component:systemui-runtime-b",
                                        "component:systemui-runtime-c")
                                .contains(reference),
                "PATCH_DEPENDENCY_MISSING",
                "Registered donor adapter is missing",
                5);
        Set<String> requested = new TreeSet<>();

        for (var selected : pins.entrySet()) {
            Failure.require(
                    selected.getValue() == null,
                    "PATCH_CLASS_INVALID",
                    "Unmodified donor classes required",
                    5);
            requested.add("L" + selected.getKey().replace('.', '/') + ";");
        }

        Failure.require(
                NxpSettingsLibrary.slot(profile.referenceProfile(), api, reference, requested),
                "PATCH_CLASS_INVALID",
                "Exact NXP SystemUI donor scope required",
                5);
        donorSlots.put(
                reference,
                DexOperations.donorSlot(
                        adapter,
                        requested,
                        dir.resolve("slot-" + reference.substring(reference.indexOf(':') + 1)),
                        api));
    }
}
