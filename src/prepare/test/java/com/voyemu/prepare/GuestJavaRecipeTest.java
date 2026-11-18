package com.voyemu.prepare;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.Map;

public final class GuestJavaRecipeTest {
    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static Path profile(Path root, String id, Boolean local) throws Exception {
        Path directory = root.resolve("profiles/" + id);

        Files.createDirectories(directory);
        Map<String, Object> data = new LinkedHashMap<>();
        data.put("schema", 1);
        data.put("android", Map.of("package", "system-images;android-30;default;arm64-v8a"));
        data.put("components", Map.of());
        data.put("patches", Map.of());

        if (local != null) {
            data.put("local", local);
        }

        Files.writeString(directory.resolve("profile.json"), Json.write(data));

        return directory;
    }

    private static GuestJava.Unit unit(Path profile, String name) throws Exception {
        Path source = profile.resolve("patches/" + name + "/src");

        Files.createDirectories(source);
        Files.writeString(
                source.resolve(name + ".java"),
                "package fixture; public class " + name + " { public int value() { return 1; } }");

        return new GuestJava.Unit(source, 30);
    }

    public static void main(String[] args) throws Exception {
        Path sdk = Path.of(args[0]), root = Files.createDirectories(Path.of(args[1])).toRealPath();
        Path platform = sdk.resolve("platforms/android-34/android.jar");
        Path source = root.resolve("src/app");

        Files.createDirectories(source);
        Map<String, Object> manifest =
                Map.of("sdk", Map.of("compileSdk", 34, "buildTools", "36.1.0"));
        Files.writeString(source.resolve("toolchain.json"), Json.write(manifest));
        Path observer = root.resolve("src/guest/android/api");
        Files.createDirectories(observer);
        Files.writeString(
                observer.resolve("Observer.java"), "package fixture; public class Observer {}");

        String localId = "a-local", defaultId = "b-public", publicId = "c-public";
        Path local = profile(root, localId, true);
        Path ordinary = profile(root, defaultId, null);
        Path published = profile(root, publicId, false);
        GuestJava.Unit localUnit = unit(local, "LocalOnly");
        GuestJava.Unit ordinaryUnit = unit(ordinary, "Ordinary");
        GuestJava.Unit publicUnit = unit(published, "Published");
        GuestJava.Unit sharedUnit = unit(local, "Shared");
        unit(ordinary, "Shared");
        ProfileRegistry registry = new ProfileRegistry(root);
        check(registry.list().size() == 3, "the source registry still exposes local profiles");

        Path localRoot = root.resolve(".local");
        Storage storage =
                new Storage(
                        localRoot,
                        localRoot.resolve("toolchains"),
                        localRoot.resolve("toolchains/downloads"),
                        localRoot.resolve("build"),
                        localRoot.resolve("build/tmp"),
                        localRoot.resolve("build/logs"));
        ProfileRegistry selected = new ProfileRegistry(root, localId);
        var profile = selected.get(localId);
        Artifacts artifacts =
                new Artifacts(
                        profile,
                        storage,
                        new Toolchains(storage, manifest, sdk, null, null),
                        selected.sourceDirectory(profile),
                        root.resolve("local-preparation"),
                        logical -> {
                            throw new AssertionError("Unexpected Android input: " + logical);
                        },
                        ignored -> {});
        check(
                artifacts.guestJavaUnits().contains(localUnit),
                "selected local preparation retains its Java unit");
        check(
                Files.isRegularFile(
                        GuestJava.dex(localUnit, platform, root.resolve("local-dex"))
                                .resolve("classes.dex")),
                "selected local Java preparation still compiles");

        Path output = root.resolve("recipe");
        GuestJavaRecipe.main(new String[] {root.toString(), sdk.toString(), output.toString()});
        Map<String, byte[]> entries = NativeCore.entries(output.resolve("android-java.zip"));
        String indexText = new String(entries.get("index.json"), StandardCharsets.UTF_8);
        Map<String, Object> index = Json.object(Json.parse(indexText));
        check(
                entries.containsKey(GuestJava.key(ordinaryUnit, platform) + "/classes.dex"),
                "a profile without local metadata is exported");
        check(
                entries.containsKey(GuestJava.key(publicUnit, platform) + "/classes.dex"),
                "an explicitly nonlocal profile is exported");
        check(
                !entries.containsKey(GuestJava.key(localUnit, platform) + "/classes.dex"),
                "a local profile unit must not be exported");
        check(!indexText.contains(localId), "the public ZIP index must not name a local profile");
        check(
                Json.str(Json.obj(index, GuestJava.key(sharedUnit, platform)), "profile")
                        .equals(defaultId),
                "a shared unit is indexed by its public profile");
        check(entries.containsKey("observer/classes.dex"), "the observer is still exported");
        System.out.println("PASS GuestJavaRecipeTest");
    }
}
