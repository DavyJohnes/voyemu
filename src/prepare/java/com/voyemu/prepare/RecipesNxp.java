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
                target.originalSha256()
                        .equals("a20c1ee2c682aad846b93010356d434ef7e43ffb314b21186a91f348ba4c497b"),
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
                            NxpSettingsLibrary.PROFILE, target.api(), expected.get(index), types),
                    "Exact SystemUI donor class inventory required");
        }

        Recipes.literalCalls(name, metadata, target);
    }

    static void systemUiRouting(String name, Map<String, Object> metadata, Recipes.Target target)
            throws Exception {
        require(
                target.originalSha256()
                        .equals("a20c1ee2c682aad846b93010356d434ef7e43ffb314b21186a91f348ba4c497b"),
                "Exact NXP SystemUI APK required");
        Recipes.literalCalls(name, metadata, target);
    }

    static void musicMedia(String name, Map<String, Object> metadata, Recipes.Target target)
            throws Exception {
        require(
                target.profile().equals("free-09-chinese-20221230-nxp-rc45")
                        && target.originalSha256()
                                .equals(
                                        "c080bcb4a663e113bd0e7caa1f17948dce2de6a0eabec9b41a93aef5641f01c3"),
                "Exact NXP Music APK required");
        Recipes.literalCalls(name, metadata, target);
    }

    static void launcherRouting(String name, Map<String, Object> metadata, Recipes.Target target)
            throws Exception {
        require(
                target.originalSha256()
                        .equals("93f4bea56530bf2de8634b7898fd4275a3c8985ff0392fdc58607ef8bc5b2441"),
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
                target.profile().equals("free-09-chinese-20221230-nxp-rc45")
                        && target.api() == 28
                        && target.originalSha256()
                                .equals(
                                        "c4920cc36a257755335c6fae098172e30414711e9f2adf3fab6dbc566edabd15")
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
                target.profile().equals("free-09-chinese-20221230-nxp-rc45")
                        && target.originalSha256()
                                .equals(
                                        "b3a79c9cb737b5c84256c98b9e00553467bc9fda43b975398a7284a0a9cac741")
                        && Json.str(constants, "INPUT_SHA256").equals(target.originalSha256()),
                "Exact NXP Settings APK/profile required");
        require(
                metadata.containsKey("sourceSha256"),
                "Pinned NXP Voice initialization recipe required");
        Recipes.literalCalls(name, metadata, target);
    }

    static void phoneProvider(Recipes.Target target) throws Exception {
        require(
                target.profile().equals("free-09-chinese-20221230-nxp-rc45")
                        && target.api() == 28
                        && target.originalSha256()
                                .equals(
                                        "55e7602d15f5d677a5ddeaaa44c0c04c355235c5322b51edefa3a6ef29636451"),
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
                target.profile().equals("free-09-chinese-20221230-nxp-rc45")
                        && target.api() == 28
                        && target.abi().equals("arm64-v8a")
                        && target.originalSha256()
                                .equals(
                                        "619186d34262ea53e5386abb52ca11d74636da2a85434fafa19df7efecca565b")
                        && Json.str(constants, "INPUT_SHA256").equals(target.originalSha256()),
                "Exact NXP Voice APK/profile/API/ABI required");
        Recipes.literalCalls(name, metadata, target);
    }

    static void voiceNatives(Map<String, Object> constants, Recipes.Target target)
            throws Exception {
        require(
                target.profile().equals("free-09-chinese-20221230-nxp-rc45")
                        && target.api() == 28
                        && target.abi().equals("arm64-v8a")
                        && target.originalSha256()
                                .equals(
                                        "619186d34262ea53e5386abb52ca11d74636da2a85434fafa19df7efecca565b")
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
        NativeCore.pinned(
                Recipes.use(target, "original:voice-libduidns.so"),
                "c244b29b93ceb84c854007c7f71d047f074e28f5ffe70280d0f6e67eb839d14c");
        NativeCore.pinned(
                Recipes.use(target, "original:voice-libsemantic.so"),
                "0a2fcb38d030507e3685e029dcb5571df7b36ce295b80485f0fa2c199cd99f55");
        NativeCore.pinned(
                Recipes.use(target, "original:voice-libvprint.so"),
                "ec31ef57eb7df7a1816b4ffcc813a02f7603a8998ba9f100a6227cca56e31f83");
        NativeCore.pinned(
                Recipes.use(target, "original:voice-libfespcar.so"),
                "98e16278ae93a049cdda63efaa40914b2c7b5b41357c28565ed9d30f98adaf58");
        NativeCore.pinned(
                Recipes.use(target, "original:voice-libduiutils.so"),
                "581e3a4aee7a260e5a453830cf23980b9e1b2b5b8b1fa1fd7fc6c2ac51e33e37");
        NativeCore.pinned(
                Recipes.use(target, "original:voice-liblame.so"),
                "16a43e9e01284e2f25b82e0932f8c37ee420b322e67210d551278bfc09269231");
        NativeCore.pinned(
                Recipes.use(target, "original:voice-libdmasp.so"),
                "3b36a92e1480959bcea8784c4c191da071dd37b66e8c0ff8417b31e951b8464c");
        NativeCore.pinned(
                Recipes.use(target, "original:voice-libvad.so"),
                "79339d3e74a6c97b63b619af7b913abcb65c4e44da4b4bfab430f11c0261b83a");
        NativeCore.pinned(
                Recipes.use(target, "original:voice-libecho.so"),
                "b52d1350c569e4f0b5be4ef9870979f38e942f84075645f1334c71951eef9a74");
        NativeCore.pinned(
                Recipes.use(target, "original:voice-libasr.so"),
                "67ee0e72d87b65f21a45405bc97c6f3f063befcc6bd4ddc20ab14b8506deb8db");
        NativeCore.pinned(
                Recipes.use(target, "original:voice-libgram.so"),
                "5a83f14223c87f23e1981c455f2627f4b719ca67586f9438e16b8277c832b4c5");
        NativeCore.pinned(
                Recipes.use(target, "original:voice-libasrpp.so"),
                "659d74319abf4115d2a2cd99d7035a6e5c28800498e02d19f800fc95f00dfd5a");
        NativeCore.pinned(
                Recipes.use(target, "original:voice-libwakeup.so"),
                "192dedade4d9fc6c5a44efc08a9a8f3cb5a3053552ae6ee4e4496698cb7fdd31");
        NativeCore.pinned(
                Recipes.use(target, "original:voice-libcntts.so"),
                "45263cc77da60e247525b7ba6bad4fae5db6cf7ccabec00f46e77656c77b65d5");
        NativeCore.pinned(
                Recipes.use(target, "original:voice-libliteca.so"),
                "b68ad678221a6d03bb304e11acef29c10134d102f4e0d1e063c794d5ae2e5791");
        NativeCore.pinned(
                Recipes.use(target, "original:voice-libSpeechRecord.so"),
                "82c873f01dc124043f26009d804cb167e28e7d49e57e373c68b086fc46fa7cf8");
        NativeCore.pinned(
                Recipes.use(target, "original:voice-libSpeechRecord4Mic.so"),
                "f95e1bb5ca4f1da739d0839f27b4232a3d258ad9487e4defa8e5c544650c5f6d");
        NativeCore.pinned(
                Recipes.use(target, "original:voice-libqgspeechagc.so"),
                "143c2a368815bfb63b2b89ca506fd79fd0da3e75276ead5001eba455a3881190");
        NativeCore.pinned(
                Recipes.use(target, "original:voice-libqgvad.so"),
                "e2810905cb9c1a26e96fcaa34f1a4b30c29e4787e3ff8b3154dd3ffeba10104f");
        NativeCore.pinned(
                Recipes.use(target, "original:voice-libvoiceengnie.so"),
                "bff1b0da6510feb9fad8702ae7a70674e58a90f762c6e7c87d633c76b822c4b7");
        NativeCore.pinned(
                Recipes.use(target, "original:voice-libdasock.so"),
                "3168f8af196804a6e2b035255309ddeeb1d4e8e10092708981e2ebd0893faf68");
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
