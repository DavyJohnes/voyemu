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
    private static final List<String> FILES =
            List.of(INPUT + "PhoneControlProvider.java", INPUT + "PhonePeerProvider.java");

    private static void need(boolean value, String message) {
        Failure.require(value, "PHONE_PROVIDER_CONTRACT", message, 5);
    }

    /**
     * The repository paths of the provider helper sources the step declares: the two providers
     * under one profiles/ directory.
     */
    static List<String> sourceInventory(Map<String, Object> constants) {
        List<String> paths = StepConstants.texts(constants, "helperSources");

        need(
                paths.size() == FILES.size()
                        && paths.get(0).endsWith(FILES.get(0))
                        && paths.get(0).startsWith("profiles/")
                        && !paths.get(0).contains("..")
                        && paths.get(1).equals(directory(paths) + FILES.get(1)),
                "Exact shared Phone provider sources required");

        return paths;
    }

    // The source root that holds com/voyemu/input/ of the declared helper sources.
    private static String directory(List<String> paths) {
        return paths.get(0).substring(0, paths.get(0).length() - FILES.get(0).length());
    }

    static Map<String, byte[]> sources(
            ProfileRegistry.Profile profile, Map<String, Object> constants) throws Exception {
        Map<String, byte[]> result = new TreeMap<>();

        for (String path : sourceInventory(constants)) {
            result.put(path, ProfileRegistry.bytes(profile, "voyemu/" + path));
        }

        verifySources(constants, result);

        return result;
    }

    static void verifySources(Map<String, Object> constants, Map<String, byte[]> sources)
            throws Exception {
        need(
                sources.keySet().equals(Set.copyOf(sourceInventory(constants))),
                "Exact shared Phone provider sources required");
    }

    static byte[] manifest(
            Map<String, Object> constants, int api, String abi, Path original, byte[] manifest)
            throws Exception {
        need(api == 30 && "arm64-v8a".equals(abi), "Exact API30 ARM64 profile required");
        // Acquisition integrity only; semantic acceptance follows.
        need(
                StepConstants.admits(constants, FilesSupport.hash(original)),
                "Pinned OEM Phone original required");
        PhoneConsumerContract.verify(PhoneConsumerContract.read(original));

        return PhoneManifest.add(manifest);
    }

    /** The repository source directory of the provider helper the step declares. */
    static Path source(ProfileRegistry.Profile profile, Map<String, Object> constants)
            throws Exception {
        need(profile.sourceRoot() != null, "Repository Phone provider sources required");

        return Storage.confined(profile.sourceRoot(), directory(sourceInventory(constants)));
    }

    static Path compile(Path source, Path platform, Path work) throws Exception {
        need(
                GuestJava.sources(source)
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
