package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public final class OwnAudioComponentsProducerTest {
    static void reject(Runnable action) {
        try {
            action.run();
            throw new AssertionError("invalid graph accepted");
        } catch (Failure expected) {
            // Rejection is the expected outcome.
        }
    }

    static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]).toRealPath();
        var original =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve(
                                                "profiles/"
                                                        + OwnAudioComponentsProducer.PROFILE
                                                        + "/profile.json"))));

        OwnAudioComponentsProducer.graph(original);
        OwnAudioComponentsProducer.graph(original, OwnAudioComponentsProducer.CANBUS_COMPONENTS);
        int negatives = 0;
        negatives += checkComponentArguments();
        negatives += checkGraphNegatives(original);
        check(OwnAudioComponentsProducer.COMPONENTS.equals(List.of("audiopolicy", "bootstrap")));
        Path stage =
                Files.createTempDirectory(
                        root.resolve(".local/build/tmp"), "own-audio-producer-guards-");

        try {
            Map<String, String> sources =
                    Map.of(
                            "components/audiopolicy/src/Own.java",
                            "a".repeat(64),
                            "components/bootstrap/src/Own.java",
                            "b".repeat(64));
            String prefix = "profiles/" + OwnAudioComponentsProducer.PROFILE + "/";
            var expected = new TreeMap<String, String>();

            for (var e : sources.entrySet()) {
                expected.put(prefix + e.getKey(), e.getValue());
            }

            Path frozen = stage.resolve("freeze.json");
            Files.writeString(frozen, Json.write(expected));
            negatives += checkTwoComponentFreeze(root, frozen, sources, expected, prefix);
            var triple = new TreeMap<>(sources);
            triple.put("components/canbus/src/Own.java", "d".repeat(64));
            triple.put("components/canbus/component.json", "e".repeat(64));
            var tripleFreeze = new TreeMap<String, String>();

            for (var e : triple.entrySet()) {
                tripleFreeze.put(prefix + e.getKey(), e.getValue());
            }

            Files.writeString(frozen, Json.write(tripleFreeze));
            negatives += checkCanbusFreeze(root, frozen, triple, tripleFreeze, expected, prefix);
            var legacyPins =
                    OwnAudioComponentsProducer.sourcePins(
                            root.resolve("profiles/" + OwnAudioComponentsProducer.PROFILE));
            var combinedPins =
                    OwnAudioComponentsProducer.sourcePins(
                            root.resolve("profiles/" + OwnAudioComponentsProducer.PROFILE),
                            OwnAudioComponentsProducer.CANBUS_COMPONENTS);
            check(legacyPins.keySet().stream().noneMatch(k -> k.startsWith("components/canbus/")));
            check(combinedPins.entrySet().containsAll(legacyPins.entrySet()));
            check(combinedPins.keySet().stream().anyMatch(k -> k.startsWith("components/canbus/")));
            negatives += checkInputPins(root, stage);
            Files.delete(frozen);
        } finally {
            Files.deleteIfExists(stage.resolve("input"));
            Files.deleteIfExists(stage.resolve("freeze.json"));
            Files.delete(stage);
        }

        System.out.println(
                "PASS legacy two-component and explicit three-component source graph, complete"
                        + " CanBus freeze, exact input pins; "
                        + negatives
                        + " inverse guards");
    }

    private static int checkComponentArguments() {
        int negatives = 0;

        check(
                OwnAudioComponentsProducer.components(new String[] {"r", "s", "c", "f"})
                        .equals(List.of("audiopolicy", "bootstrap")));
        check(
                OwnAudioComponentsProducer.components(new String[] {"r", "s", "c", "f", "--canbus"})
                        .equals(List.of("audiopolicy", "bootstrap", "canbus")));

        for (String[] invalid :
                List.of(
                        new String[] {"r", "s", "c"},
                        new String[] {"r", "s", "c", "f", "canbus"},
                        new String[] {"r", "s", "c", "f", "--canbus", "extra"},
                        new String[] {"r", "s", "c", "f", "--navbus"})) {
            reject(() -> OwnAudioComponentsProducer.components(invalid));
            negatives++;
        }

        return negatives;
    }

    private static int checkGraphNegatives(Map<String, Object> original) {
        int negatives = 0;
        var noCanbus = Json.object(Json.parse(Json.write(original)));
        var noCanbusInstall = new ArrayList<>(Json.list(noCanbus.get("install")));

        noCanbusInstall.removeIf(v -> "component:canbus".equals(Json.object(v).get("artifact")));
        noCanbus.put("install", noCanbusInstall);
        OwnAudioComponentsProducer.graph(noCanbus);
        reject(() ->
                OwnAudioComponentsProducer.graph(
                        noCanbus, OwnAudioComponentsProducer.CANBUS_COMPONENTS));
        negatives++;
        var wrong = Json.object(Json.parse(Json.write(original)));
        var components = Json.obj(wrong, "components");
        components.put("audiopolicy", "components/foreign");
        wrong.put("components", components);
        var wrongSource = wrong;
        reject(() -> OwnAudioComponentsProducer.graph(wrongSource));
        negatives++;
        wrong = Json.object(Json.parse(Json.write(original)));
        var install = new ArrayList<>(Json.list(wrong.get("install")));
        install.removeIf(v -> Json.object(v).get("artifact").equals("component:bootstrap"));
        wrong.put("install", install);
        var missing = wrong;
        reject(() -> OwnAudioComponentsProducer.graph(missing));
        negatives++;
        wrong = Json.object(Json.parse(Json.write(original)));
        install = new ArrayList<>(Json.list(wrong.get("install")));
        install.add(Map.of("artifact", "component:bootstrap", "type", "apk"));
        wrong.put("install", install);
        var duplicate = wrong;
        reject(() -> OwnAudioComponentsProducer.graph(duplicate));
        negatives++;

        wrong = Json.object(Json.parse(Json.write(original)));
        install = new ArrayList<>(Json.list(wrong.get("install")));

        for (int i = 0; i < install.size(); i++) {
            var entry = Json.object(install.get(i));

            if ("component:canbus".equals(entry.get("artifact"))) {
                entry.remove("target");
                install.set(i, entry);
            }
        }

        wrong.put("install", install);
        var obsoleteCanbus = wrong;
        reject(() ->
                OwnAudioComponentsProducer.graph(
                        obsoleteCanbus, OwnAudioComponentsProducer.CANBUS_COMPONENTS));
        negatives++;

        return negatives;
    }

    private static int checkTwoComponentFreeze(
            Path root,
            Path frozen,
            Map<String, String> sources,
            Map<String, String> expected,
            String prefix)
            throws Exception {
        int negatives = 0;

        check(OwnAudioComponentsProducer.freeze(root, frozen, sources).equals(expected));
        var altered = new TreeMap<>(expected);
        altered.put(prefix + "components/bootstrap/src/Own.java", "c".repeat(64));
        Files.writeString(frozen, Json.write(altered));

        try {
            OwnAudioComponentsProducer.freeze(root, frozen, sources);
            throw new AssertionError();
        } catch (Failure required) {
            negatives++;
        }

        altered.remove(prefix + "components/bootstrap/src/Own.java");
        Files.writeString(frozen, Json.write(altered));

        try {
            OwnAudioComponentsProducer.freeze(root, frozen, sources);
            throw new AssertionError();
        } catch (Failure required) {
            negatives++;
        }

        return negatives;
    }

    private static int checkCanbusFreeze(
            Path root,
            Path frozen,
            Map<String, String> triple,
            Map<String, String> tripleFreeze,
            Map<String, String> expected,
            String prefix)
            throws Exception {
        int negatives = 0;

        check(
                OwnAudioComponentsProducer.freeze(
                                root, frozen, triple, OwnAudioComponentsProducer.CANBUS_COMPONENTS)
                        .equals(tripleFreeze));
        check(OwnAudioComponentsProducer.freeze(root, frozen, triple).equals(expected));

        for (String key :
                List.of("components/canbus/src/Own.java", "components/canbus/component.json")) {
            var incomplete = new TreeMap<>(tripleFreeze);
            incomplete.remove(prefix + key);
            Files.writeString(frozen, Json.write(incomplete));

            try {
                OwnAudioComponentsProducer.freeze(
                        root, frozen, triple, OwnAudioComponentsProducer.CANBUS_COMPONENTS);
                throw new AssertionError("incomplete CanBus freeze accepted");
            } catch (Failure required) {
                negatives++;
            }
        }

        var changed = new TreeMap<>(tripleFreeze);
        changed.put(prefix + "components/canbus/src/Own.java", "f".repeat(64));
        Files.writeString(frozen, Json.write(changed));

        try {
            OwnAudioComponentsProducer.freeze(
                    root, frozen, triple, OwnAudioComponentsProducer.CANBUS_COMPONENTS);
            throw new AssertionError("changed CanBus freeze accepted");
        } catch (Failure required) {
            negatives++;
        }

        return negatives;
    }

    private static int checkInputPins(Path root, Path stage) throws Exception {
        int negatives = 0;
        Path input = stage.resolve("input");

        Files.writeString(input, "exact production input");
        Map<Path, String> tracked = new HashMap<>();
        OwnAudioComponentsProducer.pin(root, input, FilesSupport.hash(input), tracked);
        check(tracked.size() == 1);

        try {
            OwnAudioComponentsProducer.pin(root, input, "0".repeat(64), tracked);
            throw new AssertionError();
        } catch (IllegalArgumentException required) {
            check("INPUT_SHA_MISMATCH".equals(required.getMessage()));
            negatives++;
        }

        Files.delete(input);

        return negatives;
    }
}
