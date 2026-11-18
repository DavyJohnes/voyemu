package com.voyemu.prepare;

import java.lang.reflect.InvocationTargetException;
import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

/** Explicit opt-in builder integration test; uses pinned existing dependencies, never downloads. */
public final class AudioPolicyResourceTransportTest {
    static final List<String> SOURCES =
            List.of(
                    "src/guest/android/oem/audio/OemAudioPolicy.java",
                    "src/guest/android/oem/audio/OemAudioPolicyUnavailable.java");

    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static void build(String[] args, Path output, Path snapshot, boolean rejected)
            throws Exception {
        boolean failed = false;

        try {
            Class.forName("com.voyemu.prepare.JavaHelperBuild")
                    .getMethod("main", String[].class)
                    .invoke(
                            null,
                            (Object)
                                    new String[] {
                                        args[0],
                                        args[1],
                                        args[2],
                                        output.toString(),
                                        snapshot == null ? "-" : snapshot.toString(),
                                        "offline",
                                        args[4]
                                    });
        } catch (InvocationTargetException error) {
            Throwable cause = error.getCause();

            if (!(cause instanceof IllegalArgumentException)
                    || !(cause.getMessage().contains("AudioPolicy snapshot")
                            || cause.getMessage().contains("Unlisted audio source"))) {
                throw error;
            }

            failed = true;
        }

        check(failed == rejected, "builder snapshot rejection differs");

        if (rejected) {
            check(!Files.exists(output), "rejected snapshot published helper");
        } else {
            var receipt =
                    Json.object(Json.parse(Files.readString(output.resolve("build-receipt.json"))));
            check(
                    Boolean.valueOf(snapshot != null).equals(receipt.get("embedded_profiles")),
                    "helper resources must not imply embedded profiles");
            var pins = Json.obj(receipt, "resource_snapshot");
            check(
                    pins.containsKey("voyemu/profile-index.json") == (snapshot != null),
                    "profile index inventory differs");

            if (snapshot == null) {
                check(
                        pins.keySet()
                                .equals(
                                        SOURCES.stream()
                                                .map(path -> "voyemu/" + path)
                                                .collect(java.util.stream.Collectors.toSet())),
                        "normal helper embeds exactly two source resources");
            }

            try (var jar =
                    new java.util.jar.JarFile(output.resolve("voyemu-java-helper.jar").toFile())) {
                check(
                        (jar.getJarEntry("voyemu/profile-index.json") != null)
                                == (snapshot != null),
                        "normal helper must not embed profile index");

                for (String path : SOURCES) {
                    check(
                            jar.getJarEntry("voyemu/" + path) != null,
                            "owned helper source missing from JAR");
                }

                if (snapshot != null) {
                    for (String name : pins.keySet()) {
                        check(jar.getJarEntry(name) != null, "allowed existing resource lost");
                    }
                }
            }
        }
    }

    public static void main(String[] args) throws Exception {
        check(
                args.length == 5,
                "root cache sdk fresh-owned-work archive-cache; JavaHelperBuild must be on test"
                        + " classpath");
        Path root = Path.of(args[0]).toRealPath(), work = Path.of(args[3]);
        check(
                work.startsWith(root.resolve(".local/build/tmp")) && !Files.exists(work),
                "fresh private work required");
        Files.createDirectories(work);
        var source = new ProfileRegistry.Profile("fixture", "test", Map.of(), "", root);
        Path normal = work.resolve("normal");
        build(args, normal, null, false);

        try (var loader =
                new URLClassLoader(
                        new java.net.URL[] {
                            normal.resolve("voyemu-java-helper.jar").toUri().toURL()
                        },
                        ClassLoader.getPlatformClassLoader())) {
            Class<?> registry = loader.loadClass("com.voyemu.prepare.ProfileRegistry");
            var read = registry.getDeclaredMethod("bytes", String.class);
            read.setAccessible(true);

            for (String path : SOURCES) {
                check(
                        Arrays.equals(
                                Files.readAllBytes(root.resolve(path)),
                                (byte[]) read.invoke(null, "voyemu/" + path)),
                        "source and embedded bytes differ");
            }
        }

        boolean refused = false;

        try {
            ProfileRegistry.bytes(source, "voyemu/src/guest/android/oem/audio/Unlisted.java");
        } catch (Failure expected) {
            refused = expected.code.equals("RESOURCE_MISSING");
        }

        check(refused, "unlisted source mapped");
        Path snapshot = work.resolve("snapshot");
        Files.createDirectories(snapshot.resolve("voyemu"));
        Files.writeString(snapshot.resolve("voyemu/profile-index.json"), "{}\n");
        build(args, work.resolve("missing"), snapshot, true);

        for (String path : SOURCES) {
            Path target = snapshot.resolve("voyemu/" + path);
            Files.createDirectories(target.getParent());
            Files.copy(root.resolve(path), target);
        }

        Path existing = snapshot.resolve("voyemu/src/guest/android/existing/Existing.java");
        Files.createDirectories(existing.getParent());
        Files.writeString(existing, "class Existing {}\n");
        Files.writeString(
                snapshot.resolve("voyemu/existing-public-resource.txt"),
                "existing public resource\n");
        build(args, work.resolve("valid"), snapshot, false);
        Path changed = snapshot.resolve("voyemu/" + SOURCES.get(0));
        byte[] original = Files.readAllBytes(changed);
        Files.writeString(changed, "wrong source\n");
        build(args, work.resolve("wrong"), snapshot, true);
        Files.write(changed, original);
        byte[] mutated = original.clone();
        mutated[mutated.length / 2] ^= 1;
        Files.write(changed, mutated);
        build(args, work.resolve("mutated"), snapshot, true);
        Files.write(changed, original);
        Files.writeString(changed.resolveSibling("Unlisted.java"), "class Unlisted {}\n");
        build(args, work.resolve("unlisted"), snapshot, true);
        System.out.println(
                "AudioPolicy source/embedded identity and missing/wrong/mutated/unlisted snapshots"
                        + " passed");
    }
}
