package com.voyemu.prepare;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.TreeMap;

/**
 * The android-java recipe: compiles the guest Java units of every public profile and the UI
 * observer with javac and D8 of the JDK it runs on and writes OUTPUT/android-java.zip with
 * KEY/classes*.dex, observer/classes.dex and index.json. The workflow runs it with the pinned JDK;
 * no OEM input is read.
 */
public final class GuestJavaRecipe {
    private GuestJavaRecipe() {}

    public static void main(String[] args) throws Exception {
        Failure.require(args.length == 3, "ANDROID_JAVA_RECIPE", "ROOT SDK OUTPUT required", 2);
        Path root = Path.of(args[0]).toRealPath(),
                sdk = Path.of(args[1]).toAbsolutePath().normalize(),
                output = Path.of(args[2]).toAbsolutePath().normalize();
        Failure.require(
                !Files.exists(output, LinkOption.NOFOLLOW_LINKS),
                "ANDROID_JAVA_RECIPE",
                "Fresh output required",
                2);
        Path local = root.resolve(".local");
        Storage storage =
                new Storage(
                        local,
                        local.resolve("toolchains"),
                        local.resolve("toolchains/downloads"),
                        local.resolve("build"),
                        local.resolve("build/tmp"),
                        local.resolve("build/logs"));
        Toolchains tools =
                new Toolchains(
                        storage,
                        Json.object(
                                Json.parse(
                                        Files.readString(root.resolve("src/app/toolchain.json")))),
                        sdk,
                        null,
                        null);
        ProfileRegistry registry = new ProfileRegistry(root);
        AudioPolicyGuard.useHelperSource(root.resolve(AudioPolicyGuard.HELPER_DIRECTORY));
        Path work = output.resolveSibling("." + output.getFileName() + ".work");
        remove(work);
        Storage.directory(output);
        Map<String, Object> index = new TreeMap<>();
        Map<String, byte[]> entries = new TreeMap<>();

        for (ProfileRegistry.Profile profile : registry.list()) {
            if (Boolean.TRUE.equals(profile.data().get("local"))) {
                continue;
            }

            Artifacts artifacts =
                    new Artifacts(
                            profile,
                            storage,
                            tools,
                            registry.sourceDirectory(profile),
                            work.resolve(profile.id()),
                            logical -> {
                                throw new Failure(
                                        "ANDROID_JAVA_RECIPE",
                                        "Guest Java reads no Android input: " + logical,
                                        5);
                            },
                            System.err::println);
            int count = 0;

            for (GuestJava.Unit unit : artifacts.guestJavaUnits()) {
                String key = GuestJava.key(unit, tools.platform());

                if (index.containsKey(key)) {
                    continue;
                }

                Path dex =
                        GuestJava.compile(
                                unit, tools.platform(), work.resolve(profile.id() + "-" + count++));

                for (var file : FilesSupport.tree(dex).entrySet()) {
                    entries.put(key + "/" + file.getKey(), Files.readAllBytes(file.getValue()));
                }

                Map<String, Object> entry = new LinkedHashMap<>();
                entry.put("profile", profile.id());
                entry.put(
                        "unit",
                        (unit.root().startsWith(work) ? work : root)
                                .relativize(unit.root())
                                .toString()
                                .replace('\\', '/'));
                entry.put("api", unit.api());
                index.put(key, entry);
                System.err.println("Compiled " + profile.id() + " " + unit.root() + " " + key);
            }
        }

        // The UI observer of src/guest/android/api without its host tests, which build reads when
        // the Java runtime of preparation has no javac.
        Path observer = work.resolve("observer/source");
        Map<String, Object> observerSources = new TreeMap<>();
        Storage.directory(observer);

        try (var files = Files.list(root.resolve("src/guest/android/api"))) {
            for (Path file : files.sorted().toList()) {
                String name = file.getFileName().toString();

                if (name.endsWith(".java") && !name.endsWith("Test.java")) {
                    Files.copy(file, observer.resolve(name));
                    observerSources.put(name, FilesSupport.hash(file));
                }
            }
        }

        Path observerDex =
                GuestJava.compile(
                        new GuestJava.Unit(observer, 28),
                        tools.platform(),
                        work.resolve("observer/work"));

        for (var file : FilesSupport.tree(observerDex).entrySet()) {
            entries.put("observer/" + file.getKey(), Files.readAllBytes(file.getValue()));
        }

        Map<String, Object> observerEntry = new LinkedHashMap<>();
        observerEntry.put("api", 28);
        observerEntry.put("sources", observerSources);
        index.put("observer", observerEntry);
        entries.put("index.json", (Json.write(index) + "\n").getBytes(StandardCharsets.UTF_8));
        NativeCore.zip(output.resolve("android-java.zip"), entries, false);
        remove(work);
    }

    private static void remove(Path path) throws Exception {
        if (Files.exists(path, LinkOption.NOFOLLOW_LINKS)) {
            try (var walk = Files.walk(path)) {
                for (Path p : walk.sorted(Comparator.reverseOrder()).toList()) {
                    Files.delete(p);
                }
            }
        }
    }
}
