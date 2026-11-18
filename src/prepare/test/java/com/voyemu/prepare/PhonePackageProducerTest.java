package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public final class PhonePackageProducerTest {
    static void reject(Runnable action) {
        try {
            action.run();
            throw new AssertionError("Invalid graph accepted");
        } catch (Failure expected) {
            // Rejection is the expected outcome.
        }
    }

    static Map<String, Object> copy(Map<String, Object> value) {
        return Json.object(Json.parse(Json.write(value)));
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]).toRealPath();
        var data =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve(
                                                "profiles/"
                                                        + PhonePackageProducer.PROFILE
                                                        + "/profile.json"))));

        PhonePackageProducer.graph(data);
        int count = 0;

        for (String field : List.of("steps", "uses", "input", "sign", "type")) {
            var altered = copy(data);
            var patches = Json.obj(altered, "patches");
            var patch = Json.object(patches.get("BluetoothPhone"));
            patch.put(
                    field,
                    field.equals("steps")
                            ? List.of("phone-provider.json")
                            : field.equals("uses") ? List.of() : "foreign");
            patches.put("BluetoothPhone", patch);
            altered.put("patches", patches);
            reject(() -> PhonePackageProducer.graph(altered));
            count++;
        }

        var wrong = copy(data);
        var components = Json.obj(wrong, "components");
        components.put("qg-runtime", "components/foreign");
        wrong.put("components", components);
        reject(() -> PhonePackageProducer.graph(wrong));
        count++;
        var missing = copy(data);
        var install = new ArrayList<>(Json.list(missing.get("install")));
        install.removeIf(n -> "patched:BluetoothPhone".equals(Json.object(n).get("artifact")));
        missing.put("install", install);
        reject(() -> PhonePackageProducer.graph(missing));
        count++;
        Path stage =
                Files.createTempDirectory(
                        root.resolve(".local/build/tmp"), "phone-producer-guards-");

        try {
            String source =
                    "profiles/"
                            + PhonePackageProducer.PROFILE
                            + "/patches/phone-provider/src/Provider.java";
            var sources = Map.of(source, "a".repeat(64));
            Path freeze = stage.resolve("freeze.json");
            Files.writeString(freeze, Json.write(sources));

            if (!PhonePackageProducer.freeze(freeze, sources).equals(sources)) {
                throw new AssertionError();
            }

            for (var bad : List.of(Map.of(), Map.of(source, "b".repeat(64)))) {
                Files.writeString(freeze, Json.write(bad));

                try {
                    PhonePackageProducer.freeze(freeze, sources);
                    throw new AssertionError();
                } catch (Failure expected) {
                    count++;
                }
            }

            Files.delete(freeze);
        } finally {
            Files.deleteIfExists(stage.resolve("freeze.json"));
            Files.delete(stage);
        }

        var original = new TreeMap<String, String>();

        for (int i = 0; i < 51798; i++) {
            original.put("Lfixture/Original;->m" + i + "()V", "a".repeat(64));
        }

        original.put(PhonePackageProducer.WINDOW_METHOD, "b".repeat(64));
        var changed = new TreeMap<>(original);
        changed.put(PhonePackageProducer.WINDOW_METHOD, "c".repeat(64));
        var proof = PhonePackageProducer.methodPreservation(original, changed);

        if (!proof.get("unchanged_original_methods").equals(51798)
                || !proof.get("all_original_methods_semantically_preserved").equals(false)) {
            throw new AssertionError("Dishonest original preservation");
        }

        count++;
        reject(() -> PhonePackageProducer.methodPreservation(original, original));
        count++;
        var second = new TreeMap<>(changed);
        second.put("Lfixture/Original;->m0()V", "d".repeat(64));
        reject(() -> PhonePackageProducer.methodPreservation(original, second));
        count++;
        var deleted = new TreeMap<>(changed);
        deleted.remove("Lfixture/Original;->m1()V");
        reject(() -> PhonePackageProducer.methodPreservation(original, deleted));
        count++;
        var missingTarget = new TreeMap<>(changed);
        missingTarget.remove(PhonePackageProducer.WINDOW_METHOD);
        reject(() -> PhonePackageProducer.methodPreservation(original, missingTarget));
        count++;
        System.out.println(
                "PASS exact production Phone recipe and single approved OEM method proof, all"
                        + " dependencies, whole provider freeze; "
                        + count
                        + " inverse guards");
    }
}
