package com.voyemu.prepare;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** API28 recipe handlers for the NXP RC45 profile. */
final class RecipesNxp {
    static void systemUiDonors(String name, Map<String, Object> metadata, Recipes.Target target)
            throws Exception {
        require(
                target.originalSha256().equals(target.originalPin()),
                "Exact NXP SystemUI APK required");
        List<Object> calls = Json.list(metadata.get("calls"));
        List<String> expected =
                name.equals("nxp-systemui-media-library.json")
                        ? List.of("component:media-callbacks")
                        : List.of(
                                "component:settings-runtime",
                                "component:settings-managers",
                                "component:systemui-runtime-a",
                                "component:systemui-runtime-b",
                                "component:systemui-runtime-c");
        require(calls.size() == expected.size(), "Exact SystemUI donor slots required");

        for (int index = 0; index < calls.size(); index++) {
            Map<String, Object> call = Json.object(calls.get(index));
            require(
                    Json.str(call, "operation").equals("donorSlot"),
                    "Only embedded donor slots are allowed");
            List<Object> args = Json.list(call.get("args"));
            require(
                    args.size() == 2 && expected.get(index).equals(args.get(0)),
                    "Exact donor slot order required");
            Set<String> types = new HashSet<>();

            for (var entry : Json.object(args.get(1)).entrySet()) {
                require(entry.getValue() == null, "Unmodified donor classes required");
                types.add("L" + entry.getKey().replace('.', '/') + ";");
            }

            require(
                    NxpSettingsLibrary.slot(
                            target.profile(), target.api(), expected.get(index), types),
                    "Exact SystemUI donor class inventory required");
        }

        Recipes.literalCalls(name, metadata, target);
    }

    static void systemUiRouting(String name, Map<String, Object> metadata, Recipes.Target target)
            throws Exception {
        require(
                target.originalSha256().equals(target.originalPin()),
                "Exact NXP SystemUI APK required");
        Recipes.literalCalls(name, metadata, target);
    }

    static void musicMedia(String name, Map<String, Object> metadata, Recipes.Target target)
            throws Exception {
        require(
                target.originalSha256().equals(target.originalPin()),
                "Exact NXP Music APK required");
        Recipes.literalCalls(name, metadata, target);
    }

    static void launcherRouting(String name, Map<String, Object> metadata, Recipes.Target target)
            throws Exception {
        require(
                target.originalSha256().equals(target.originalPin()),
                "Exact NXP Launcher APK required");
        Recipes.literalCalls(name, metadata, target);
    }

    static void systemPolicyHardware(
            String name,
            Map<String, Object> metadata,
            Map<String, Object> constants,
            Recipes.Target target)
            throws Exception {
        require(
                target.api() == 28
                        && target.originalSha256().equals(target.originalPin())
                        && Json.str(constants, "INPUT_SHA256").equals(target.originalSha256()),
                "Exact NXP SystemPolicy APK/profile/API required");
        require(
                metadata.containsKey("sourceSha256"),
                "Pinned NXP SystemPolicy hardware recipe required");

        Recipes.literalCalls(name, metadata, target);
    }

    static void settingsVoiceInitialization(
            String name,
            Map<String, Object> metadata,
            Map<String, Object> constants,
            Recipes.Target target)
            throws Exception {
        require(
                target.originalSha256().equals(target.originalPin())
                        && Json.str(constants, "INPUT_SHA256").equals(target.originalSha256()),
                "Exact NXP Settings APK/profile required");
        require(
                metadata.containsKey("sourceSha256"),
                "Pinned NXP Voice initialization recipe required");
        Recipes.literalCalls(name, metadata, target);
    }

    static void phoneProvider(Recipes.Target target) throws Exception {
        require(
                target.api() == 28 && target.originalSha256().equals(target.originalPin()),
                "Exact NXP Phone APK/profile required");
        target.addNxpPhoneProvider();
    }

    static void phoneConnectAnr(
            String name,
            Map<String, Object> metadata,
            Map<String, Object> constants,
            Recipes.Target target)
            throws Exception {
        require(
                target.api() == 28
                        && target.abi().equals("arm64-v8a")
                        && target.originalSha256().equals(target.originalPin())
                        && Json.str(constants, "INPUT_SHA256").equals(target.originalSha256()),
                "Exact NXP Voice APK/profile/API/ABI required");
        Recipes.literalCalls(name, metadata, target);
    }

    static void voiceNatives(Map<String, Object> constants, Recipes.Target target)
            throws Exception {
        require(
                target.api() == 28
                        && target.abi().equals("arm64-v8a")
                        && target.originalSha256().equals(target.originalPin())
                        && Json.str(constants, "INPUT_SHA256").equals(target.originalSha256()),
                "Exact NXP Voice APK/profile/API/ABI required");
        require(
                target.uses()
                        .keySet()
                        .equals(
                                Set.of(
                                        "component:qg-runtime",
                                        "original:voice-libduidns.so",
                                        "original:voice-libsemantic.so",
                                        "original:voice-libvprint.so",
                                        "original:voice-libfespcar.so",
                                        "original:voice-libduiutils.so",
                                        "original:voice-liblame.so",
                                        "original:voice-libdmasp.so",
                                        "original:voice-libvad.so",
                                        "original:voice-libecho.so",
                                        "original:voice-libasr.so",
                                        "original:voice-libgram.so",
                                        "original:voice-libasrpp.so",
                                        "original:voice-libwakeup.so",
                                        "original:voice-libcntts.so",
                                        "original:voice-libliteca.so",
                                        "original:voice-libSpeechRecord.so",
                                        "original:voice-libSpeechRecord4Mic.so",
                                        "original:voice-libqgspeechagc.so",
                                        "original:voice-libqgvad.so",
                                        "original:voice-libvoiceengnie.so",
                                        "original:voice-libdasock.so")),
                "Exact NXP Voice native dependencies required");
        requirePinnedVoiceNatives(target);
        target.removeEntry("lib/armeabi/libSpeechRecord.so");
        target.putEntry(
                Recipes.use(target, "original:voice-libduidns.so"), "lib/arm64-v8a/libduidns.so");
        target.putEntry(
                Recipes.use(target, "original:voice-libsemantic.so"),
                "lib/arm64-v8a/libsemantic.so");
        target.putEntry(
                Recipes.use(target, "original:voice-libvprint.so"), "lib/arm64-v8a/libvprint.so");
        target.putEntry(
                Recipes.use(target, "original:voice-libfespcar.so"), "lib/arm64-v8a/libfespcar.so");
        target.putEntry(
                Recipes.use(target, "original:voice-libduiutils.so"),
                "lib/arm64-v8a/libduiutils.so");
        target.putEntry(
                Recipes.use(target, "original:voice-liblame.so"), "lib/arm64-v8a/liblame.so");
        target.putEntry(
                Recipes.use(target, "original:voice-libdmasp.so"), "lib/arm64-v8a/libdmasp.so");
        target.putEntry(Recipes.use(target, "original:voice-libvad.so"), "lib/arm64-v8a/libvad.so");
        target.putEntry(
                Recipes.use(target, "original:voice-libecho.so"), "lib/arm64-v8a/libecho.so");
        target.putEntry(Recipes.use(target, "original:voice-libasr.so"), "lib/arm64-v8a/libasr.so");
        target.putEntry(
                Recipes.use(target, "original:voice-libgram.so"), "lib/arm64-v8a/libgram.so");
        target.putEntry(
                Recipes.use(target, "original:voice-libasrpp.so"), "lib/arm64-v8a/libasrpp.so");
        target.putEntry(
                Recipes.use(target, "original:voice-libwakeup.so"), "lib/arm64-v8a/libwakeup.so");
        target.putEntry(
                Recipes.use(target, "original:voice-libcntts.so"), "lib/arm64-v8a/libcntts.so");
        target.putEntry(
                Recipes.use(target, "original:voice-libliteca.so"), "lib/arm64-v8a/libliteca.so");
        target.putEntry(
                Recipes.use(target, "original:voice-libSpeechRecord.so"),
                "lib/arm64-v8a/libSpeechRecord.so");
        target.putEntry(
                Recipes.use(target, "original:voice-libSpeechRecord4Mic.so"),
                "lib/arm64-v8a/libSpeechRecord4Mic.so");
        target.putEntry(
                Recipes.use(target, "original:voice-libqgspeechagc.so"),
                "lib/arm64-v8a/libqgspeechagc.so");
        target.putEntry(
                Recipes.use(target, "original:voice-libqgvad.so"), "lib/arm64-v8a/libqgvad.so");
        target.putEntry(
                Recipes.use(target, "original:voice-libvoiceengnie.so"),
                "lib/arm64-v8a/libvoiceengnie.so");
        target.putEntry(
                Recipes.use(target, "original:voice-libdasock.so"), "lib/arm64-v8a/libdasock.so");
    }

    private static void requirePinnedVoiceNatives(Recipes.Target target) throws Exception {
        // Each native library carries the pin that original of the profile declares.
        for (String reference : target.uses().keySet()) {
            if (reference.startsWith("original:")) {
                NativeCore.pinned(Recipes.use(target, reference), target.originalPin(reference));
            }
        }
    }

    static void selinuxPolicy(String name, Map<String, Object> constants, Recipes.Target target)
            throws Exception {
        Path file = Recipes.output(target);

        require(
                FilesSupport.hash(file).equals(Json.str(constants, "INPUT_SHA256")),
                "Exact clean API28 policy/context/hash input required");
        String result;

        if (name.equals("selinux-hash-nxp.json")) {
            require(
                    target.uses().keySet().equals(Set.of("patched:selinux-canbus-nxp")),
                    "Only patched NXP canbus policy dependency required");
            // API28 init compares this identity to the vendor precompiled policy; a changed
            // CIL forces secilc.
            result = FilesSupport.hash(Recipes.use(target, "patched:selinux-canbus-nxp")) + "\n";
        } else {
            // The OEM policies feed selinux-bounded-su-nxp, the step after the canbus append.
            require(
                    target.uses()
                            .keySet()
                            .equals(
                                    name.equals("selinux-canbus-nxp.json")
                                            ? Set.of(
                                                    "original:PlatSepolicy",
                                                    "original:VendorSepolicy")
                                            : Set.of()),
                    "NXP policy/context donor dependency differs");
            result = Files.readString(file) + Json.str(constants, "APPEND");
        }

        require(
                NativeCore.hash(result.getBytes(StandardCharsets.UTF_8))
                        .equals(Json.str(constants, "OUTPUT_SHA256")),
                "Exact NXP policy/context/hash output required");
        Files.writeString(file, result);
    }

    private static void require(boolean condition, String message) {
        Recipes.require(condition, message);
    }
}
