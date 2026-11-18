package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * The Original Navi recipes take intermediate artifacts that preparation builds from pinned inputs
 * and own sources, so their allowlists stay empty; two fresh builds must agree byte for byte.
 */
public final class OriginalNaviInputsDeterminismTest {
    private static final String PROFILE = "free-11-chinese-20260731-7.3.1";

    private static void require(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    public static void main(String[] args) throws Exception {
        require(args.length == 5, "ROOT PLATFORM FRAMEWORK_JAR SERVICES_JAR STAGE required");
        Path root = Path.of(args[0]),
                platform = Path.of(args[1]),
                framework = Path.of(args[2]),
                services = Path.of(args[3]),
                stage = Path.of(args[4]);
        Files.createDirectories(stage);
        var profile = new ProfileRegistry(root, PROFILE).get(PROFILE);
        var tools =
                new Toolchains(
                        null,
                        Map.of(),
                        root.resolve(".local/toolchains/native-sdk"),
                        null,
                        null,
                        platform);
        List<String> references =
                List.of(
                        "component:view-observation",
                        "patched:api30-secondary-navigation",
                        "patched:original-navi-framework",
                        "patched:original-navi-services");
        List<List<String>> builds = new ArrayList<>();

        for (String build : List.of("first", "second")) {
            var artifacts =
                    new Artifacts(
                            new Artifacts.PatchedProfile(
                                    profile, ProfileRegistry.patchMetadata(profile)),
                            null,
                            tools,
                            root.resolve("profiles/" + PROFILE),
                            stage.resolve(build),
                            path ->
                                    switch (path) {
                                        case "/system/framework/framework.jar" -> framework;
                                        case "/system/framework/services.jar" -> services;
                                        default ->
                                                throw new AssertionError(
                                                        "Unexpected Android read: " + path);
                                    },
                            line -> {});
            List<String> hashes = new ArrayList<>();

            for (String reference : references) {
                hashes.add(FilesSupport.hash(artifacts.resolve(reference)));
            }

            builds.add(hashes);
        }

        require(
                builds.get(0).equals(builds.get(1)),
                "Two builds of the Original Navi inputs differ: " + builds);
        System.out.println("PASS two builds agree byte for byte: " + builds.get(0));
    }
}
