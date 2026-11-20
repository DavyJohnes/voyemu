package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public final class Api30RecipesTest {
    static final String APK = "a".repeat(64), ENTRY = "lib/armeabi-v7a/optional.so";
    static final String PROFILE = "free-11-chinese-20260731-7.3.1";
    static final byte[] BYTES = {1, 2, 3};

    static final class Target implements Recipes.Target {
        int naviVinValidations,
                launcherDvrValidations,
                cameraValidations,
                mutations,
                kaolaValidations,
                galleryValidations,
                fullscreenValidations,
                musicValidations,
                networkValidations,
                strValidations,
                launcherPowerValidations,
                aqyValidations,
                routingValidations,
                audioPolicyValidations,
                audioConsumerValidations,
                notificationValidations,
                systemPolicyValidations;
        String original = APK;
        String profile = PROFILE;
        Map<String, byte[]> entries =
                new HashMap<>(
                        Map.of(
                                ENTRY,
                                BYTES,
                                "classes.dex",
                                new byte[] {4},
                                "res/own",
                                new byte[] {5}));

        @Override
        public String profile() {
            return profile;
        }

        @Override
        public int api() {
            return 30;
        }

        @Override
        public String abi() {
            return "arm64-v8a";
        }

        @Override
        public Path work() {
            return null;
        }

        @Override
        public Path output() {
            return null;
        }

        @Override
        public Map<String, Path> uses() {
            return Map.of();
        }

        @Override
        public String originalSha256() {
            return original;
        }

        @Override
        public void addDex(String s) {
            throw new AssertionError();
        }

        @Override
        public void putEntry(Path p, String n) {
            throw new AssertionError();
        }

        @Override
        public void removeEntry(String n) {
            mutations++;
            entries.remove(n);
        }

        @Override
        public void useLibraries(List<String> n) {
            mutations++;
        }

        @Override
        public void verifyPinnedEntry(String n, String pin) throws Exception {
            Artifacts.verifyPinnedNativeEntry(entries, n, pin);
        }

        @Override
        public void routeSystemUi(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                routingValidations++;
            }
        }

        @Override
        public void guardMediaCloudAvailability(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                kaolaValidations++;
            }
        }

        @Override
        public void guardMusicFullscreen(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                fullscreenValidations++;
            }
        }

        @Override
        public void guardNaviVin(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                naviVinValidations++;
            }
        }

        @Override
        public void guardLauncherDvr(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                launcherDvrValidations++;
            }
        }

        @Override
        public void guardCameraVirtual(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                cameraValidations++;
            }
        }

        @Override
        public void guardSystemPolicyHardware(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                systemPolicyValidations++;
            }
        }

        @Override
        public void guardGalleryArm64(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                galleryValidations++;
            }
        }

        @Override
        public void guardMusicPage(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                musicValidations++;
            }
        }

        @Override
        public void guardNetwork(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                networkValidations++;
            }
        }

        @Override
        public void guardAqyProvider(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                aqyValidations++;
            }
        }

        @Override
        public void guardLauncherPower(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                launcherPowerValidations++;
            }
        }

        @Override
        public void guardAudioPolicy(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                audioPolicyValidations++;
            }
        }

        @Override
        public void guardAudioConsumer(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                audioConsumerValidations++;
            }
        }

        @Override
        public void guardMediaNotification(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                notificationValidations++;
            }
        }

        @Override
        public void guardStrPower(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                strValidations++;
            }
        }
    }

    static Map<String, Object> recipe(List<Object> pins, List<Object> calls) {
        return Map.of(
                "api",
                30,
                "sourceSha256",
                "b".repeat(64),
                "constants",
                Map.of("original_sha256_allowlist", pins),
                "calls",
                calls);
    }

    static Object call(String operation, Object... args) {
        return Map.of("operation", operation, "args", List.of(args));
    }

    static void rejectCalls(String name, String pin, List<Object> calls, String message)
            throws Exception {
        var reject = new Target();

        reject.original = pin;
        boolean denied = false;

        try {
            Recipes.apply(name, recipe(List.of(pin), calls), reject);
        } catch (Failure expected) {
            denied = true;
        }

        if (!denied || reject.mutations != 0) {
            throw new AssertionError(message);
        }
    }

    static void rejectOwnerPin(String name, Object guard, String message) throws Exception {
        var reject = new Target();
        boolean denied = false;

        try {
            Recipes.apply(name, recipe(List.of(APK), List.of(guard)), reject);
        } catch (Failure expected) {
            denied = true;
        }

        if (!denied || reject.mutations != 0) {
            throw new AssertionError(message);
        }
    }

    static void rejectHelperPath(String name, String pin, Map<String, Object> valid, String message)
            throws Exception {
        var bad = new HashMap<String, Object>(valid);

        bad.put("helper", "wrong");
        var denied = new Target();
        denied.original = pin;
        boolean caught = false;

        try {
            Recipes.apply(name, bad, denied);
        } catch (Failure expected) {
            caught = true;
        }

        if (!caught || denied.mutations != 0) {
            throw new AssertionError(message);
        }
    }

    public static void main(String[] args) throws Exception {
        checkGallery();
        checkSystemPolicy();
        var remove = call("removePinnedEntry", ENTRY, NativeCore.hash(BYTES));
        var libraries = call("useLibraries", List.of("QGAPI"));
        checkGenericOperations(remove, libraries);
        checkHelperPinnedGuards();
        checkMusicFullscreen();
        checkMusicPage();
        checkCloudAvailability();
        checkNetwork(libraries);
        checkStrPower(libraries);
        checkLauncherPower(libraries);
        checkAqyProvider(libraries);
        checkSystemUiRouting();

        for (String operation :
                List.of("guardAudioPolicy", "guardAudioConsumer", "guardMediaNotification")) {
            checkRegisteredOperation(operation);
        }

        System.out.println(
                "API30 allowlisted source/entry pins and complete prevalidation passed; no guest"
                        + " acceptance");
    }

    private static void checkGallery() throws Exception {
        var gallery = new Target();

        gallery.original = GalleryArm64Libraries.pins(PROFILE).get(0);
        Recipes.apply(
                GalleryArm64Libraries.NAME + ".json",
                recipe(
                        List.of(GalleryArm64Libraries.pins(PROFILE).get(0)),
                        List.of(call("guardGalleryArm64"))),
                gallery);

        if (gallery.mutations != 1 || gallery.galleryValidations != 1) {
            throw new AssertionError("Gallery must prevalidate before mutation");
        }

        for (var calls :
                List.of(
                        List.of(call("guardGalleryArm64"), call("guardGalleryArm64")),
                        List.of(call("guardGalleryArm64", "extra")))) {
            rejectCalls(
                    GalleryArm64Libraries.NAME + ".json",
                    GalleryArm64Libraries.pins(PROFILE).get(0),
                    calls,
                    "Invalid Gallery operations mutated");
        }
    }

    private static void checkSystemPolicy() throws Exception {
        String name = SystemPolicyHardwareGuard.NAME + ".json",
                pin = "a81710fcbb382d9a1c449b240c2dc40aebc3d2e3cc309339ec1be3c5fbb2de00";
        Object guard = call("guardSystemPolicyHardware"),
                libraries = call("useLibraries", List.of("QGAPI", "android.car"));
        var policy = new Target();

        policy.original = pin;
        Recipes.apply(name, recipe(List.of(pin), List.of(guard, libraries)), policy);

        if (policy.mutations != 2 || policy.systemPolicyValidations != 1) {
            throw new AssertionError("SystemPolicy must prevalidate before mutation");
        }

        for (var calls :
                List.of(
                        List.of(libraries, guard),
                        List.of(guard),
                        List.of(guard, guard, libraries),
                        List.of(call("guardSystemPolicyHardware", "extra"), libraries),
                        List.of(guard, call("useLibraries", List.of("QGAPI"))),
                        List.of(guard, call("useLibraries", List.of("QGAPI", "QGMOD"))))) {
            rejectCalls(name, pin, calls, "Invalid SystemPolicy operations mutated");
        }

        rejectCalls("own-api30.json", pin, List.of(guard, libraries), "SystemPolicy guard renamed");
    }

    private static void checkGenericOperations(Object remove, Object libraries) throws Exception {
        var target = new Target();

        Recipes.apply("own-api30.json", recipe(List.of(APK), List.of(libraries, remove)), target);

        if (target.mutations != 2
                || target.entries.containsKey(ENTRY)
                || target.entries.size() != 2
                || target.entries.get("classes.dex")[0] != 4
                || target.entries.get("res/own")[0] != 5) {
            throw new AssertionError("Generic operations altered unrelated content");
        }

        List<Map<String, Object>> invalid =
                List.of(
                        recipe(List.of("c".repeat(64)), List.of(libraries, remove)),
                        recipe(List.of(), List.of(libraries)),
                        recipe(List.of(APK, APK), List.of(libraries)),
                        recipe(
                                List.of(APK),
                                List.of(
                                        libraries,
                                        call("removePinnedEntry", ENTRY, "0".repeat(64)))),
                        recipe(
                                List.of(APK),
                                List.of(
                                        libraries,
                                        call(
                                                "removePinnedEntry",
                                                "lib/armeabi-v7a/missing.so",
                                                NativeCore.hash(BYTES)))),
                        recipe(List.of(APK), List.of(libraries, call("addDex", "source"))),
                        recipe(List.of(APK), List.of(call("removeEntry", ENTRY))));

        for (var metadata : invalid) {
            var reject = new Target();
            boolean failed = false;

            try {
                Recipes.apply("own-api30.json", metadata, reject);
            } catch (Failure expected) {
                failed = true;
            }

            if (!failed || reject.mutations != 0) {
                throw new AssertionError("Invalid recipe mutated before full validation");
            }
        }

        boolean failed = false;

        try {
            Recipes.apply(
                    "add-qg-runtime.json", recipe(List.of(APK), List.of(libraries)), new Target());
        } catch (Failure expected) {
            failed = true;
        }

        if (!failed) {
            throw new AssertionError("API28 named handler accepted on API30");
        }
    }

    private static void checkHelperPinnedGuards() throws Exception {
        var vin = new Target();

        vin.original = NaviVinGuard.PIN;
        var vinRecipe =
                new HashMap<String, Object>(
                        recipe(List.of(NaviVinGuard.PIN), List.of(call("guardNaviVin"))));
        vinRecipe.put("helper", NaviVinGuard.HELPER_PATH);
        Recipes.apply(NaviVinGuard.NAME + ".json", vinRecipe, vin);

        if (vin.mutations != 1 || vin.naviVinValidations != 1) {
            throw new AssertionError("Navi VIN plan/apply mismatch");
        }

        rejectHelperPath(
                NaviVinGuard.NAME + ".json",
                NaviVinGuard.PIN,
                vinRecipe,
                "Navi VIN helper pin accepted");
        var dvr = new Target();
        dvr.original = APK;
        var dvrRecipe =
                new HashMap<String, Object>(
                        recipe(List.of(APK), List.of(call("guardLauncherDvr"))));
        dvrRecipe.put("helper", LauncherDvrGuard.HELPER_PATH);
        Recipes.apply(LauncherDvrGuard.NAME + ".json", dvrRecipe, dvr);

        if (dvr.mutations != 1 || dvr.launcherDvrValidations != 1) {
            throw new AssertionError("Launcher DVR plan/apply mismatch");
        }

        rejectHelperPath(
                LauncherDvrGuard.NAME + ".json",
                APK,
                dvrRecipe,
                "Launcher DVR helper pin accepted");
        var camera = new Target();
        camera.original = CameraVirtualGuard.pins(PROFILE).get(0);
        var cameraRecipe =
                new HashMap<String, Object>(
                        recipe(
                                List.of(CameraVirtualGuard.pins(PROFILE).get(0)),
                                List.of(call("guardCameraVirtual"))));
        cameraRecipe.put("helper", CameraVirtualGuard.HELPER_PATH);
        Recipes.apply(CameraVirtualGuard.NAME + ".json", cameraRecipe, camera);

        if (camera.mutations != 1 || camera.cameraValidations != 1) {
            throw new AssertionError("Camera plan/apply mismatch");
        }

        rejectHelperPath(
                CameraVirtualGuard.NAME + ".json",
                CameraVirtualGuard.pins(PROFILE).get(0),
                cameraRecipe,
                "Camera helper pin accepted");
    }

    private static void checkMusicFullscreen() throws Exception {
        var fullscreenGuard = call("guardMusicFullscreen");
        var fullscreen = new Target();

        fullscreen.original = APK;
        Recipes.apply(
                MusicFullscreenGuard.NAME + ".json",
                recipe(List.of(APK), List.of(fullscreenGuard)),
                fullscreen);

        if (fullscreen.mutations != 1 || fullscreen.fullscreenValidations != 1) {
            throw new AssertionError("Music fullscreen must prevalidate before mutation");
        }

        for (var calls :
                List.of(
                        List.of(fullscreenGuard, fullscreenGuard),
                        List.of(fullscreenGuard, call("unknown")),
                        List.of(call("guardMusicFullscreen", "unexpected")))) {
            rejectCalls(
                    MusicFullscreenGuard.NAME + ".json",
                    APK,
                    calls,
                    "Invalid Music fullscreen list mutated APK");
        }

        for (String name : List.of("other-api30.json")) {
            rejectOwnerPin(
                    name, fullscreenGuard, "Music fullscreen requires exact recipe and owner pin");
        }
    }

    private static void checkMusicPage() throws Exception {
        var musicGuard = call("guardMusicPage");
        var music = new Target();

        music.original = APK;
        Recipes.apply(
                MusicPageGuard.NAME + ".json", recipe(List.of(APK), List.of(musicGuard)), music);

        if (music.mutations != 1 || music.musicValidations != 1) {
            throw new AssertionError("Music page must prevalidate before mutation");
        }

        for (var calls :
                List.of(
                        List.of(musicGuard, musicGuard),
                        List.of(musicGuard, call("unknown")),
                        List.of(call("guardMusicPage", "unexpected")))) {
            rejectCalls(
                    MusicPageGuard.NAME + ".json",
                    APK,
                    calls,
                    "Invalid Music page list mutated APK");
        }

        for (String name : List.of("other-api30.json")) {
            rejectOwnerPin(name, musicGuard, "Music page requires exact recipe and owner pin");
        }
    }

    private static void checkCloudAvailability() throws Exception {
        var kaolaGuard = call("guardMediaCloudAvailability");
        var kaola = new Target();

        kaola.original = APK;
        Recipes.apply(
                MediaCloudConfigGuard.NAME + ".json",
                recipe(List.of(APK), List.of(kaolaGuard)),
                kaola);

        if (kaola.mutations != 1 || kaola.kaolaValidations != 1) {
            throw new AssertionError("CloudFM availability must prevalidate before mutation");
        }

        for (var calls :
                List.of(
                        List.of(kaolaGuard, kaolaGuard),
                        List.of(kaolaGuard, call("unknown")),
                        List.of(call("guardMediaCloudAvailability", "unexpected")))) {
            rejectCalls(
                    MediaCloudConfigGuard.NAME + ".json",
                    APK,
                    calls,
                    "Invalid CloudFM availability list mutated APK");
        }

        for (String name : List.of("other-api30.json")) {
            rejectOwnerPin(
                    name, kaolaGuard, "CloudFM availability requires exact recipe and owner pin");
        }
    }

    private static void checkNetwork(Object libraries) throws Exception {
        var guard = call("guardNetwork");
        var network = new Target();

        network.original = "3b38818c46903a3786df70793712c2d2d043cfb516279b49fa412acf4a1e9c9c";
        Recipes.apply(
                NetworkGuard.NAME + ".json",
                recipe(
                        List.of("3b38818c46903a3786df70793712c2d2d043cfb516279b49fa412acf4a1e9c9c"),
                        List.of(libraries, guard)),
                network);

        if (network.mutations != 2 || network.networkValidations != 1) {
            throw new AssertionError("Exact network operation must prevalidate before mutation");
        }

        for (var calls :
                List.of(
                        List.of(guard, guard),
                        List.of(guard, call("unknown")),
                        List.of(call("guardNetwork", "unexpected")))) {
            rejectCalls(
                    NetworkGuard.NAME + ".json",
                    "3b38818c46903a3786df70793712c2d2d043cfb516279b49fa412acf4a1e9c9c",
                    calls,
                    "Invalid network list must not mutate");
        }

        rejectOwnerPin("other-api30.json", guard, "Network operation requires its exact recipe");
    }

    private static void checkStrPower(Object libraries) throws Exception {
        var strGuard = call("guardStrPower");
        var str = new Target();

        str.original = "6f96bbeb58bf915b0a2d60817082cdca3523e3a2d00c144544cb92b4e5be6870";
        Recipes.apply(
                StrPowerGuard.NAME + ".json",
                recipe(
                        List.of("6f96bbeb58bf915b0a2d60817082cdca3523e3a2d00c144544cb92b4e5be6870"),
                        List.of(libraries, strGuard)),
                str);

        if (str.mutations != 2 || str.strValidations != 1) {
            throw new AssertionError("STR operation must prevalidate before mutations");
        }

        for (var calls :
                List.of(
                        List.of(strGuard, strGuard),
                        List.of(strGuard, call("unknown")),
                        List.of(call("guardStrPower", "unexpected")))) {
            rejectCalls(
                    StrPowerGuard.NAME + ".json",
                    "6f96bbeb58bf915b0a2d60817082cdca3523e3a2d00c144544cb92b4e5be6870",
                    calls,
                    "Invalid STR operation list mutated APK");
        }

        rejectOwnerPin("other-api30.json", strGuard, "STR operation requires its exact recipe");
    }

    private static void checkLauncherPower(Object libraries) throws Exception {
        var mapGuard = call("guardLauncherPower");
        var map = new Target();

        map.original = APK;
        Recipes.apply(
                LauncherPowerGuard.NAME + ".json",
                recipe(List.of(APK), List.of(libraries, mapGuard)),
                map);

        if (map.mutations != 2 || map.launcherPowerValidations != 1) {
            throw new AssertionError("Map power operation must prevalidate before mutations");
        }

        for (var calls :
                List.of(
                        List.of(mapGuard, mapGuard),
                        List.of(mapGuard, call("unknown")),
                        List.of(call("guardLauncherPower", "unexpected")))) {
            rejectCalls(
                    LauncherPowerGuard.NAME + ".json",
                    APK,
                    calls,
                    "Invalid Launcher power operation list mutated APK");
        }

        for (String name : List.of("other-api30.json")) {
            rejectOwnerPin(
                    name, mapGuard, "Map power operation requires exact recipe and owner pin");
        }
    }

    private static void checkAqyProvider(Object libraries) throws Exception {
        var aqyGuard = call("guardAqyProvider");
        var aqy = new Target();

        aqy.original = APK;
        Recipes.apply(
                AqyProviderGuard.NAME + ".json",
                recipe(List.of(APK), List.of(libraries, aqyGuard)),
                aqy);

        if (aqy.mutations != 2 || aqy.aqyValidations != 1) {
            throw new AssertionError("AQY provider boundary must prevalidate before mutations");
        }

        for (var calls :
                List.of(
                        List.of(aqyGuard, aqyGuard),
                        List.of(aqyGuard, call("unknown")),
                        List.of(call("guardAqyProvider", "unexpected")))) {
            rejectCalls(
                    AqyProviderGuard.NAME + ".json",
                    APK,
                    calls,
                    "Invalid AQY provider list mutated APK");
        }

        for (String name : List.of("other-api30.json")) {
            rejectOwnerPin(
                    name, aqyGuard, "AQY provider boundary requires exact recipe and owner pin");
        }
    }

    private static void checkSystemUiRouting() throws Exception {
        var routing = call("routeSystemUi");
        var statusBars = new Target();

        statusBars.original = SystemUiRouting.pins(PROFILE).get(0);
        Recipes.apply(
                SystemUiRouting.NAME + ".json",
                recipe(List.of(SystemUiRouting.pins(PROFILE).get(0)), List.of(routing)),
                statusBars);

        if (statusBars.mutations != 1 || statusBars.routingValidations != 1) {
            throw new AssertionError("SystemUI routing must validate before one mutation");
        }

        for (var calls :
                List.of(
                        List.of(routing, routing),
                        List.of(routing, call("unknown")),
                        List.of(call("routeSystemUi", "unexpected")))) {
            rejectCalls(
                    SystemUiRouting.NAME + ".json",
                    SystemUiRouting.pins(PROFILE).get(0),
                    calls,
                    "Invalid status-bar operation mutated APK");
        }

        for (String name : List.of("other-api30.json", SystemUiRouting.NAME + ".json")) {
            rejectOwnerPin(name, routing, "SystemUI routing needs exact recipe and original owner");
        }
    }

    private static void checkRegisteredOperation(String operation) throws Exception {
        String pin =
                operation.equals("guardAudioPolicy")
                        ? "2152a99acdbee41626c6b661fe6509dd2c9487d3cad6a66aad6320bbb00cc039"
                        : operation.equals("guardAudioConsumer")
                                ? "c178682be164be4872697dbb5accdc6ea28446941b6d681ddb736f2fdb627c07"
                                : "c".repeat(64);
        String name =
                operation.equals("guardAudioPolicy")
                        ? "api30-audio-policy.json"
                        : operation.equals("guardAudioConsumer")
                                ? "api30-audio-consumer.json"
                                : MediaNotificationGuard.NAME + ".json";
        var sources = AudioPolicyGuard.HELPER_SOURCES;
        var valid = new HashMap<String, Object>(recipe(List.of(pin), List.of(call(operation))));

        if (operation.equals("guardAudioPolicy")) {
            valid.put("helperSources", sources);
            valid.put("calls", List.of(call(operation), call("useLibraries", List.of("QGAPI"))));
        }

        var audio = new Target();
        audio.original = pin;
        Recipes.apply(name, valid, audio);

        if (audio.mutations != (operation.equals("guardAudioPolicy") ? 2 : 1)
                || audio.audioPolicyValidations
                                + audio.audioConsumerValidations
                                + audio.notificationValidations
                        != 1) {
            throw new AssertionError(
                    "Exact registered operation must validate once before apply: " + operation);
        }

        for (int mode = 0; mode < (operation.equals("guardAudioPolicy") ? 16 : 5); mode++) {
            checkInvalidRegistration(operation, mode, pin, name, valid, sources);
        }
    }

    private static void checkInvalidRegistration(
            String operation,
            int mode,
            String pin,
            String name,
            Map<String, Object> valid,
            List<String> sources)
            throws Exception {
        var reject = new Target();

        reject.original = pin;
        var invalidAudio = new HashMap<String, Object>(valid);
        String invalidName = name;

        switch (mode) {
            case 0 -> reject.original = APK;
            case 1 -> invalidName = "other-api30.json";
            case 2 -> invalidAudio = new HashMap<>(recipe(List.of(APK), List.of(call(operation))));
            case 3 -> invalidAudio.put("calls", List.of(call(operation, "extra")));
            case 4 -> invalidAudio.put("calls", List.of(call(operation), call(operation)));
            case 5 -> invalidAudio.remove("helperSources");
            case 6 -> invalidAudio.put("helperSources", List.of());
            case 7 -> invalidAudio.put("helperSources", List.of(sources.get(0)));
            case 8 -> invalidAudio.put("helperSources", List.of(sources.get(1), sources.get(0)));
            case 9 -> invalidAudio.put("helperSources", List.of("src/other.java", sources.get(1)));
            case 10 ->
                    invalidAudio.put(
                            "helperSources",
                            List.of(sources.get(0), sources.get(1), "src/unlisted.java"));
            case 11 -> invalidAudio.put("calls", List.of(call(operation)));
            case 12 ->
                    invalidAudio.put(
                            "calls",
                            List.of(call("useLibraries", List.of("QGAPI")), call(operation)));
            case 13 ->
                    invalidAudio.put(
                            "calls",
                            List.of(
                                    call(operation),
                                    call("useLibraries", List.of("QGAPI", "QGMOD"))));
            case 14 -> invalidAudio.put("calls", List.of(call("useLibraries", List.of("QGAPI"))));
            case 15 ->
                    invalidAudio.put(
                            "calls",
                            List.of(
                                    call(operation),
                                    call("useLibraries", List.of("QGAPI")),
                                    call("useLibraries", List.of("QGAPI"))));
            default -> {}
        }

        boolean denied = false;

        try {
            Recipes.apply(invalidName, invalidAudio, reject);
        } catch (Failure | IllegalArgumentException expected) {
            denied = true;
        }

        if (!denied || reject.mutations != 0) {
            throw new AssertionError(
                    "Invalid registered operation mutated input: " + operation + " mode=" + mode);
        }
    }
}
