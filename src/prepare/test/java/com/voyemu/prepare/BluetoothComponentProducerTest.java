package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public final class BluetoothComponentProducerTest {
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
                                                        + BluetoothComponentProducer.PROFILE
                                                        + "/profile.json"))));

        BluetoothComponentProducer.graph(original);
        int negatives = 0;
        var wrong = Json.object(Json.parse(Json.write(original)));
        var components = Json.obj(wrong, "components");
        components.put("bluetooth", "components/foreign");
        wrong.put("components", components);
        var wrongSource = wrong;
        reject(() -> BluetoothComponentProducer.graph(wrongSource));
        negatives++;
        wrong = Json.object(Json.parse(Json.write(original)));
        var install = new ArrayList<>(Json.list(wrong.get("install")));
        install.removeIf(v -> Json.object(v).get("artifact").equals("component:bluetooth"));
        wrong.put("install", install);
        var missing = wrong;
        reject(() -> BluetoothComponentProducer.graph(missing));
        negatives++;
        wrong = Json.object(Json.parse(Json.write(original)));
        install = new ArrayList<>(Json.list(wrong.get("install")));
        install.add(
                Map.of(
                        "artifact",
                        "component:bluetooth",
                        "type",
                        "file",
                        "target",
                        "/system/framework/Ivt8BluetoothAdapter.jar",
                        "mode",
                        "0644"));
        wrong.put("install", install);
        var duplicate = wrong;
        reject(() -> BluetoothComponentProducer.graph(duplicate));
        negatives++;
        check(BluetoothComponentProducer.COMPONENTS.equals(List.of("bluetooth")));
        Path stage =
                Files.createTempDirectory(
                        root.resolve(".local/build/tmp"), "own-audio-producer-guards-");

        try {
            Map<String, String> sources =
                    Map.of("components/bluetooth/src/Own.java", "a".repeat(64));
            String prefix = "profiles/" + BluetoothComponentProducer.PROFILE + "/";
            var expected = new TreeMap<String, String>();

            for (var e : sources.entrySet()) {
                expected.put(prefix + e.getKey(), e.getValue());
            }

            Path frozen = stage.resolve("freeze.json");
            Files.writeString(frozen, Json.write(expected));
            check(BluetoothComponentProducer.freeze(root, frozen, sources).equals(expected));
            var altered = new TreeMap<>(expected);
            altered.put(prefix + "components/bluetooth/src/Own.java", "c".repeat(64));
            Files.writeString(frozen, Json.write(altered));

            try {
                BluetoothComponentProducer.freeze(root, frozen, sources);
                throw new AssertionError();
            } catch (Failure required) {
                negatives++;
            }

            altered.remove(prefix + "components/bluetooth/src/Own.java");
            Files.writeString(frozen, Json.write(altered));

            try {
                BluetoothComponentProducer.freeze(root, frozen, sources);
                throw new AssertionError();
            } catch (Failure required) {
                negatives++;
            }

            Path input = stage.resolve("input");
            Files.writeString(input, "exact production input");
            Map<Path, String> tracked = new HashMap<>();
            BluetoothComponentProducer.pin(root, input, FilesSupport.hash(input), tracked);
            check(tracked.size() == 1);

            try {
                BluetoothComponentProducer.pin(root, input, "0".repeat(64), tracked);
                throw new AssertionError();
            } catch (IllegalArgumentException required) {
                check("INPUT_SHA_MISMATCH".equals(required.getMessage()));
                negatives++;
            }

            Files.delete(input);
            Files.delete(frozen);
        } finally {
            Files.deleteIfExists(stage.resolve("input"));
            Files.deleteIfExists(stage.resolve("freeze.json"));
            Files.delete(stage);
        }

        System.out.println(
                "PASS finite two-component source graph, reviewed freeze coverage, exact input"
                        + " pins; "
                        + negatives
                        + " inverse guards");
    }
}
