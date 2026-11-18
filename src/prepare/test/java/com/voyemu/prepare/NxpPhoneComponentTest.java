package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.Set;

/** Ordinary producer path: compile-only OEM declarations must never be packaged in the adapter. */
public final class NxpPhoneComponentTest {
    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), work = Path.of(args[1]);

        Files.createDirectories(work);
        String id = "free-09-chinese-20221230-nxp-rc45";
        Path recipe = root.resolve("profiles/" + id);
        var data = Json.object(Json.parse(Files.readString(recipe.resolve("profile.json"))));
        var components = Json.obj(data, "components");
        components.put("bluetooth", "components/phone-bluetooth");
        data.put("components", components);
        var profile =
                new ProfileRegistry.Profile(
                        id,
                        "nxp-production-component-test",
                        data,
                        "voyemu/profiles/" + id + "/",
                        root);
        var storage =
                new Storage(
                        root.resolve(".local"),
                        work.resolve("config"),
                        work.resolve("cache"),
                        work.resolve("state"),
                        work.resolve("run"),
                        work.resolve("logs"));
        var manifest =
                Json.object(Json.parse(Files.readString(root.resolve("src/app/toolchain.json"))));
        var tools =
                new Toolchains(
                        storage,
                        manifest,
                        root.resolve(".local/toolchains/java-sdk"),
                        null,
                        null,
                        root.resolve(
                                ".local/toolchains/java-sdk/platforms/android-34/android.jar"));
        var artifacts =
                new Artifacts(
                        new Artifacts.PatchedProfile(profile, Map.of()),
                        storage,
                        tools,
                        recipe,
                        work.resolve("producer"),
                        path -> {
                            throw new AssertionError("Unexpected runtime input " + path);
                        },
                        System.out::println);
        Path output = artifacts.resolve("component:bluetooth");
        var entries = NativeCore.entries(output);

        if (!entries.keySet().equals(Set.of("classes.dex"))) {
            throw new AssertionError("Unexpected adapter payload");
        }

        var classes = NativeCore.classes(DexOperations.dex(output, "classes.dex", 28));

        if (classes.isEmpty()
                || classes.stream()
                        .anyMatch(name ->
                                !name.startsWith("Lcom/qinggan/bluetooth/internal/ivt8/"))) {
            throw new AssertionError("Compile-only declarations leaked " + classes);
        }

        if (!classes.contains("Lcom/qinggan/bluetooth/internal/ivt8/PeerPower;")) {
            throw new AssertionError("Missing power authority");
        }

        System.out.println(
                "NXP Phone component PASS ordinary resolver, own DEX classes="
                        + classes.size()
                        + ", no OEM declarations packaged");
    }
}
