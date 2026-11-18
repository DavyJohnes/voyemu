package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/** Exact API30 producer plumbing; the adapter applies a separate pinned OEM call-window guard. */
final class PhoneProviderGuard {
    static final String NAME = "api30-phone-provider";
    private static final String INPUT = "com/voyemu/input/";

    private record Variant(String apk, String prefix, List<String> libraries) {
        List<String> sources() {
            return List.of(prefix + "PhoneControlProvider.java", prefix + "PhonePeerProvider.java");
        }
    }

    private static final Map<String, Variant> VARIANTS =
            Map.of(
                    "free-11-chinese-20250710-6.11.1",
                    new Variant(
                            "c3b53187d5161692a338a9504e1fa288dd163896da431bf89b8ad1e8fa56b1f4",
                            "profiles/free-11-chinese-20250710-6.11.1/patches/phone-provider/src/com/voyemu/input/",
                            List.of("QGAPI", "QGMOD")),
                    "free-11-chinese-20260731-7.3.1",
                    new Variant(
                            "c8a33e377a6a708ef6c3cca7e8cc3f938349c04dd494dac32bcf823a3096efa1",
                            "profiles/free-09-chinese-20260106-6.0.3/patches/phone-provider/src/com/voyemu/input/",
                            List.of("QGAPI", "QGMOD", "QGVehicle")));

    static List<String> pins(String profile) {
        var variant = VARIANTS.get(profile);

        return variant == null ? List.of() : List.of(variant.apk());
    }

    static List<Object> calls(String profile) {
        var variant = VARIANTS.get(profile);

        return variant == null
                ? List.of()
                : List.of(
                        Map.of("operation", "guardPhoneProvider", "args", List.of()),
                        Map.of("operation", "useLibraries", "args", List.of(variant.libraries())));
    }

    static List<Object> sourceInventory(String profile) {
        var variant = VARIANTS.get(profile);

        return variant == null ? List.of() : List.copyOf(variant.sources());
    }

    private static void need(boolean value, String message) {
        Failure.require(value, "PHONE_PROVIDER_CONTRACT", message, 5);
    }

    static Map<String, byte[]> sources(ProfileRegistry.Profile profile) throws Exception {
        var variant = VARIANTS.get(profile.referenceProfile());

        need(variant != null, "Exact API30 ARM64 profile required");
        Map<String, byte[]> result = new TreeMap<>();

        for (String path : variant.sources()) {
            result.put(path, ProfileRegistry.bytes(profile, "voyemu/" + path));
        }

        verifySources(profile.referenceProfile(), result);

        return result;
    }

    static void verifySources(String profile, Map<String, byte[]> sources) throws Exception {
        need(
                VARIANTS.containsKey(profile)
                        && sources.keySet().equals(Set.copyOf(VARIANTS.get(profile).sources())),
                "Exact shared Phone provider sources required");
    }

    static byte[] manifest(String profile, int api, String abi, Path original, byte[] manifest)
            throws Exception {
        need(
                VARIANTS.containsKey(profile) && api == 30 && "arm64-v8a".equals(abi),
                "Exact API30 ARM64 profile required");
        NativeCore.pinned(
                original,
                VARIANTS.get(profile)
                        .apk()); // Acquisition integrity only; semantic acceptance follows.
        PhoneConsumerContract.verify(PhoneConsumerContract.read(original));

        return PhoneManifest.add(manifest);
    }

    /** The repository source directory of the provider helper, or null for other profiles. */
    static Path source(ProfileRegistry.Profile profile) throws Exception {
        var variant = VARIANTS.get(profile.referenceProfile());

        if (variant == null) {
            return null;
        }

        need(profile.sourceRoot() != null, "Repository Phone provider sources required");

        return Storage.confined(
                profile.sourceRoot(),
                variant.prefix().substring(0, variant.prefix().length() - INPUT.length()));
    }

    static Path compile(String profile, Path source, Path platform, Path work) throws Exception {
        need(
                VARIANTS.containsKey(profile)
                        && GuestJava.sources(source)
                                .equals(
                                        Set.of(
                                                INPUT + "PhoneControlProvider.java",
                                                INPUT + "PhonePeerProvider.java")),
                "Exact shared Phone provider sources required");
        Path dex = GuestJava.dex(new GuestJava.Unit(source, 30), platform, work.resolve("java"));

        for (var file : FilesSupport.tree(dex).values()) {
            var classes =
                    NativeCore.classes(
                            org.jf.dexlib2.DexFileFactory.loadDexFile(
                                    file.toFile(), org.jf.dexlib2.Opcodes.forApi(30)));
            need(
                    !classes.isEmpty()
                            && classes.stream()
                                    .allMatch(name ->
                                            name.matches(
                                                    "Lcom/voyemu/input/Phone(?:Control|Peer)Provider(?:\\$[^;]+)?;")),
                    "Only owned provider classes may be packaged");
        }

        return dex;
    }
}
