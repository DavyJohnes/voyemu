package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/** Signed actual Artifacts path, isolating the newly added provider from prior OEM transforms. */
public final class PhoneArtifactTest {
    static void check(boolean b, String message) {
        if (!b) {
            throw new AssertionError(message);
        }
    }

    static Map<String, String> methods(Map<String, byte[]> entries) throws Exception {
        Map<String, String> result = new TreeMap<>();

        for (var e : entries.entrySet()) {
            if (e.getKey().matches("classes[0-9]*\\.dex")) {
                for (var cls :
                        new DexBackedDexFile(Opcodes.forApi(28), e.getValue()).getClasses()) {
                    for (var method : cls.getMethods()) {
                        check(
                                result.put(
                                                method.toString(),
                                                FrameworkDexMaterializer.semantic(method))
                                        == null,
                                "duplicate method");
                    }
                }
            }
        }

        return result;
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), stage = Path.of(args[1]);

        Files.createDirectory(stage);
        String id = "free-09-chinese-20260106-6.0.3";
        var data =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve("profiles/" + id + "/profile.json"))));
        var only = Json.object(Json.obj(data, "patches").get("BluetoothPhone"));
        only.put("steps", List.of("phone-provider.json"));
        only.put("uses", List.of());
        var patches = Json.obj(data, "patches");
        patches.put("BluetoothPhone", only);
        data.put("patches", patches);
        var profile = new ProfileRegistry.Profile(id, "provider-isolated-test", data, "");
        Path local = root.resolve(".local");
        var storage =
                new Storage(
                        local,
                        local.resolve("config"),
                        local.resolve("cache"),
                        local.resolve("state"),
                        local.resolve("run"),
                        local.resolve("logs"));
        var signing =
                Json.obj(
                        Json.object(
                                Json.parse(
                                        Files.readString(
                                                local.resolve(
                                                        "inputs/" + id + "/native-inputs.json")))),
                        "signing");
        Path key = Path.of(Json.str(Json.obj(signing, "key"), "file")),
                cert = Path.of(Json.str(Json.obj(signing, "cert"), "file"));
        NativeCore.pinned(key, Json.str(Json.obj(signing, "key"), "sha256"));
        NativeCore.pinned(cert, Json.str(Json.obj(signing, "cert"), "sha256"));
        var toolchain =
                Json.object(Json.parse(Files.readString(root.resolve("src/app/toolchain.json"))));
        var tools =
                new Toolchains(
                        storage,
                        toolchain,
                        root.resolve(".local/toolchains/native-sdk"),
                        key,
                        cert,
                        root.resolve(
                                ".local/toolchains/java-sdk/platforms/android-34/android.jar"));
        Path recipe = root.resolve("profiles/" + id),
                recipeFile = recipe.resolve("patches/phone-provider.json");
        var metadata = Json.object(Json.parse(Files.readString(recipeFile)));
        metadata.put("sourceSha256", FilesSupport.hash(recipeFile));
        var builder =
                new Artifacts(
                        new Artifacts.PatchedProfile(
                                profile, Map.of("phone-provider.json", metadata)),
                        storage,
                        tools,
                        recipe,
                        stage.resolve("work"),
                        p -> {
                            throw new AssertionError("unexpected guest read");
                        },
                        System.out::println);
        Path original = storage.originals(id).resolve("BluetoothPhone.apk"),
                output = builder.resolve("patched:BluetoothPhone");
        var before = NativeCore.entries(original);
        var after = NativeCore.entries(output);
        var previous = methods(before);
        var current = methods(after);
        Api28PhoneCallWindowGuard.verify(before.get("classes.dex"), after.get("classes.dex"));
        var preservation = PhonePackageProducer.methodPreservation(previous, current);

        for (var e : before.entrySet()) {
            if (!e.getKey().equals("AndroidManifest.xml")
                    && !e.getKey().matches("classes[0-9]*\\.dex")
                    && !DexOperations.signatureEntry(e.getKey())) {
                check(
                        Arrays.equals(e.getValue(), after.get(e.getKey())),
                        "OEM non-code changed: " + e.getKey());
            }
        }

        check(
                current.keySet().stream()
                        .anyMatch(s -> s.startsWith("Lcom/voyemu/input/PhoneControlProvider;")),
                "provider absent");
        Path bluetooth = builder.resolve("component:bluetooth");

        for (var e : NativeCore.entries(bluetooth).entrySet()) {
            if (e.getKey().endsWith(".dex")) {
                for (var cls :
                        new DexBackedDexFile(Opcodes.forApi(28), e.getValue()).getClasses()) {
                    check(
                            cls.getType()
                                    .matches(
                                            "Lcom/qinggan/bluetooth/internal/ivt8/(?:Ivt8BTAdapter|SyntheticPhonePeer|PeerConfiguration|PeerSubscription)(?:\\$[^;]+)?;"),
                            "OEM compile-only ABI packaged: " + cls.getType());
                }
            }
        }

        Files.writeString(
                stage.resolve("receipt.json"),
                Json.write(
                                Map.of(
                                        "isolated_new_step",
                                        true,
                                        "signed_apk",
                                        output.toString(),
                                        "sha256",
                                        FilesSupport.hash(output),
                                        "all_original_methods_semantically_preserved",
                                        false,
                                        "unchanged_original_methods",
                                        51798,
                                        "approved_oem_method_delta",
                                        preservation.get("approved_oem_method_delta"),
                                        "actual_bluetooth_component",
                                        bluetooth.toString(),
                                        "vm_started",
                                        false))
                        + "\n");
        System.out.println(
                "PASS signed actual Artifacts provider, exact one approved change; original"
                        + " methods="
                        + previous.size()
                        + ", component finite compile-only wiring");
    }
}
