package com.voyemu.prepare;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.NoSuchAlgorithmException;
import java.util.Map;
import java.util.TreeMap;

/** Exercises installed CI-built native component receipts through the production Java resolver. */
public final class NativeComponentHandoffTest {
    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]).toRealPath(), base = Path.of(args[1]).toRealPath();
        String id = "free-09-chinese-20260106-6.0.3";
        Path recipe = root.resolve("profiles").resolve(id);
        var data = Json.object(Json.parse(Files.readString(recipe.resolve("profile.json"))));
        var profile = new ProfileRegistry.Profile(id, "test", data, "", root);
        Map<String, Object> entries =
                new TreeMap<>(Json.object(Json.parse(Files.readString(Path.of(args[2])))));
        long declared = 0;

        for (var component : Json.obj(data, "components").entrySet()) {
            Path source = Storage.confined(recipe, (String) component.getValue());
            var definition =
                    Json.object(Json.parse(Files.readString(source.resolve("component.json"))));

            if ("native".equals(definition.get("type"))) {
                declared++;
            }
        }

        PreparationMain.PinnedInput pinned = pinnedResolver();
        var outputs =
                PreparationMain.nativeComponentOutputs(
                        Map.of("native_components", entries), profile, root, pinned);

        if (declared == 0 || outputs.size() != declared || entries.size() != declared) {
            throw new AssertionError("All declared native components required");
        }

        var builder = builder(profile, recipe, base);

        for (var entry : outputs.entrySet()) {
            builder.useNativeComponent(entry.getKey(), entry.getValue());

            if (!builder.resolve(entry.getKey()).equals(entry.getValue())) {
                throw new AssertionError("Native component dispatched to a Java build");
            }
        }

        var first = outputs.entrySet().iterator().next();
        expectFailure(() -> builder.useNativeComponent(first.getKey(), first.getValue()));
        expectFailure(() -> builder(profile, recipe, base).resolve(first.getKey()));
        checkBadHandoffs(entries, profile, root, pinned);
        System.out.println(
                "All declared native components resolved from CI-built receipts; a second"
                        + " override, a missing handoff and six bad handoffs per component"
                        + " rejected");
    }

    interface Call {
        void run() throws Exception;
    }

    static void expectFailure(Call call) throws Exception {
        try {
            call.run();
        } catch (Failure e) {
            return;
        }

        throw new AssertionError("Native component misuse accepted");
    }

    static Artifacts builder(ProfileRegistry.Profile profile, Path recipe, Path base)
            throws Exception {
        return new Artifacts(
                new Artifacts.PatchedProfile(profile, Map.of()),
                null,
                null,
                recipe,
                base.resolve("resolver-work-" + System.nanoTime()),
                logical -> {
                    throw new AssertionError("Guest read");
                },
                message -> {});
    }

    static PreparationMain.PinnedInput pinnedResolver() {
        return (file, pin) -> {
            try {
                Failure.require(
                        Files.isRegularFile(file)
                                && !Files.isSymbolicLink(file)
                                && FilesSupport.hash(file).equals(pin),
                        "TEST_PIN",
                        "Pin differs",
                        3);

                return file;
            } catch (IOException | NoSuchAlgorithmException e) {
                throw new RuntimeException(e);
            }
        };
    }

    static void checkBadHandoffs(
            Map<String, Object> entries,
            ProfileRegistry.Profile profile,
            Path root,
            PreparationMain.PinnedInput pinned)
            throws Exception {
        for (String name : entries.keySet()) {
            for (int mode = 0; mode < 6; mode++) {
                var bad = new TreeMap<>(entries);
                var item = new TreeMap<>(Json.object(bad.get(name)));

                if (mode == 0) {
                    bad.remove(name);
                }

                if (mode == 1) {
                    bad.put("unknown", item);
                }

                if (mode == 2) {
                    item.put("receipt_sha256", "0".repeat(64));
                }

                if (mode == 3) {
                    item.put("sha256", "0".repeat(64));
                }

                if (mode == 4) {
                    item.put("output", "../outside");
                }

                if (mode == 5) {
                    item =
                            new TreeMap<>(
                                    Json.object(
                                            entries.get(
                                                    entries.keySet().stream()
                                                            .filter(other -> !other.equals(name))
                                                            .findFirst()
                                                            .orElseThrow())));
                }

                if (mode >= 2) {
                    bad.put(name, item);
                }

                var request = Map.<String, Object>of("native_components", bad);
                expectFailure(() ->
                        PreparationMain.nativeComponentOutputs(request, profile, root, pinned));
            }
        }
    }
}
