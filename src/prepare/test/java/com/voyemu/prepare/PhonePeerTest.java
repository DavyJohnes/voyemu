package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Field;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableField;
import org.jf.dexlib2.immutable.ImmutableMethod;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/** Exercises the actual ordinary component resolver and own source resource pipeline. */
public final class PhonePeerTest {
    static final String ID = BluetoothPeerComponent.PROFILE, PREFIX = BluetoothPeerComponent.PREFIX;

    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static Map<String, Object> copy(Map<String, Object> value) {
        return Json.object(Json.parse(Json.write(value)));
    }

    static ProfileRegistry.Profile profile(Path root, Map<String, Object> data) {
        return new ProfileRegistry.Profile(
                ID, "isolated-production-component", data, "voyemu/profiles/" + ID + "/", root);
    }

    static Artifacts artifacts(
            Path root, Path recipe, Path work, Storage storage, Map<String, Object> data)
            throws Exception {
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

        return new Artifacts(
                new Artifacts.PatchedProfile(profile(root, data), Map.of()),
                storage,
                tools,
                recipe,
                work,
                path -> {
                    throw new AssertionError(
                            "component unexpectedly requested Android runtime input: " + path);
                },
                System.out::println);
    }

    static void reject(Map<String, ClassDef> altered, String message) {
        try {
            BluetoothPeerComponent.contract(altered);
            throw new AssertionError("accepted " + message);
        } catch (Failure required) {
            check(required.code.equals("QINGGAN11_PHONE_PEER_ABI"), required.code);
        }
    }

    static Map<String, ClassDef> changed(Map<String, ClassDef> classes, boolean field) {
        String key = PREFIX + (field ? "profile/SimpleHfpCallback" : "profile/IHfpCallback") + ";";
        ClassDef owner = classes.get(key);
        var altered = new TreeMap<>(classes);
        var fields = new ArrayList<Field>();

        for (Field value : owner.getFields()) {
            fields.add(
                    field && value.getName().equals("currentDevice")
                            ? new ImmutableField(
                                    value.getDefiningClass(),
                                    value.getName(),
                                    "Ljava/lang/Object;",
                                    value.getAccessFlags(),
                                    value.getInitialValue(),
                                    value.getAnnotations(),
                                    value.getHiddenApiRestrictions())
                            : value);
        }

        var methods = new ArrayList<Method>();

        for (Method value : owner.getMethods()) {
            methods.add(
                    !field && value.getName().equals("on3WayCallStateChanged")
                            ? new ImmutableMethod(
                                    value.getDefiningClass(),
                                    value.getName(),
                                    value.getParameters(),
                                    "I",
                                    value.getAccessFlags(),
                                    value.getAnnotations(),
                                    value.getHiddenApiRestrictions(),
                                    value.getImplementation())
                            : value);
        }

        altered.put(
                key,
                new ImmutableClassDef(
                        owner.getType(),
                        owner.getAccessFlags(),
                        owner.getSuperclass(),
                        owner.getInterfaces(),
                        owner.getSourceFile(),
                        owner.getAnnotations(),
                        fields,
                        methods));
        reject(altered, "changed ABI");

        return altered;
    }

    static void rejectThroughProducer(
            Path root,
            Path recipe,
            Path job,
            Storage storage,
            Map<String, Object> data,
            Map<String, ClassDef> altered,
            String label)
            throws Exception {
        Path dex = job.resolve("mutated-" + label + ".dex");

        org.jf.dexlib2.writer.pool.DexPool.writeTo(
                dex.toString(), new ImmutableDexFile(Opcodes.forApi(30), altered.values()));
        Path fixture = storage.originals(ID).resolve("BluetoothPhone.apk");
        NativeCore.zip(fixture, Map.of("classes.dex", Files.readAllBytes(dex)), false);
        var negative = copy(data);
        var originals = Json.obj(negative, "original");
        var phone = new LinkedHashMap<>(Json.obj(originals, "BluetoothPhone"));
        phone.put("sha256", FilesSupport.hash(fixture));
        originals.put("BluetoothPhone", phone);
        negative.put("original", originals);

        try {
            artifacts(root, recipe, job.resolve("reject-" + label), storage, negative)
                    .resolve("component:bluetooth");
            throw new AssertionError("ordinary resolver accepted " + label);
        } catch (Failure expected) {
            check(expected.code.equals("QINGGAN11_PHONE_PEER_ABI"), expected.code);
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 4) {
            throw new IllegalArgumentException(
                    "ROOT FRESH_TEST_WORK GENUINE_PHONE_APK PROFILE_METADATA");
        }

        Path root = Path.of(args[0]).toRealPath(),
                work = Path.of(args[1]).toRealPath(),
                phone = Path.of(args[2]).toRealPath(),
                metadata = Path.of(args[3]).toRealPath();
        check(work.startsWith(root.resolve(".local/build/jobs")), "isolated test work required");
        Path job = work.resolve("peer-tests");
        check(!Files.exists(job), "fresh peer fixture required");
        Storage.directory(job);
        var data = Json.object(Json.parse(Files.readString(metadata.resolve("profile.json"))));
        NativeCore.pinned(
                phone, Json.str(Json.obj(Json.obj(data, "original"), "BluetoothPhone"), "sha256"));
        var qg = Json.obj(Json.obj(data, "original"), "QGAPI");
        Path actualQG =
                root.resolve(".local/profiles/" + ID + "/originals/" + Json.str(qg, "file"));
        NativeCore.pinned(actualQG, Json.str(qg, "sha256"));
        Path recipe = job.resolve("recipe"), component = recipe.resolve("components/bluetooth");
        Storage.directory(component);
        Files.copy(
                metadata.resolve("components/bluetooth/component.json"),
                component.resolve("component.json"),
                StandardCopyOption.REPLACE_EXISTING);
        data.put("components", Map.of("bluetooth", "components/bluetooth"));
        data.put("patches", Map.of());
        data.put(
                "install",
                List.of(
                        Map.of(
                                "artifact",
                                "component:bluetooth",
                                "type",
                                "file",
                                "target",
                                "/system/framework/Ivt8BluetoothAdapter.jar",
                                "mode",
                                "0644")));
        data.put(
                "original",
                Map.of(
                        "QGAPI",
                        qg,
                        "BluetoothPhone",
                        Map.of("file", "BluetoothPhone.apk", "sha256", FilesSupport.hash(phone))));
        Path local = job.resolve("inputs");
        var storage =
                new Storage(
                        local,
                        job.resolve("config"),
                        job.resolve("cache"),
                        job.resolve("state"),
                        job.resolve("run"),
                        job.resolve("logs"));
        Path originals = storage.originals(ID);
        Storage.directory(originals.resolve("framework"));
        Files.copy(
                actualQG,
                originals.resolve(Json.str(qg, "file")),
                StandardCopyOption.REPLACE_EXISTING);
        Files.copy(
                phone,
                originals.resolve("BluetoothPhone.apk"),
                StandardCopyOption.REPLACE_EXISTING);
        Path output =
                artifacts(root, recipe, job.resolve("ordinary-component"), storage, data)
                        .resolve("component:bluetooth");
        var entries = NativeCore.entries(output);
        check(entries.keySet().equals(Set.of("classes.dex")), "single DEX-only component JAR");
        var classes = NativeCore.classes(DexOperations.dex(output, "classes.dex", 30));
        check(
                classes.size() == 38
                        && classes.stream().allMatch(n -> n.startsWith(PREFIX + "internal/ivt8/")),
                "38 own classes only");
        int negatives = 0;
        var owner = BluetoothPeerComponent.read(phone);
        BluetoothPeerComponent.contract(owner);

        for (String type :
                List.of(
                        "profile/BTAdapter",
                        "profile/SimpleHfpCallback",
                        "profile/SimplePbapCallback",
                        "profile/IHfpCallback",
                        "bean/BTContactItem")) {
            var altered = new TreeMap<>(owner);
            altered.remove(PREFIX + type + ";");
            reject(altered, "missing " + type);
            negatives++;
        }

        var changedField = changed(owner, true);
        var changedMethod = changed(owner, false);
        negatives += 2;
        var missing = new TreeMap<>(owner);
        missing.remove(PREFIX + "profile/SimplePbapCallback;");
        rejectThroughProducer(root, recipe, job, storage, data, missing, "missing-callback-owner");
        rejectThroughProducer(
                root, recipe, job, storage, data, changedField, "changed-device-field");
        rejectThroughProducer(
                root, recipe, job, storage, data, changedMethod, "changed-callback-return");
        negatives += 3;
        Files.copy(
                phone,
                originals.resolve("BluetoothPhone.apk"),
                StandardCopyOption.REPLACE_EXISTING);
        var definition =
                Json.object(Json.parse(Files.readString(component.resolve("component.json"))));
        definition.put("minSdk", 29);
        Files.writeString(component.resolve("component.json"), Json.write(definition) + "\n");

        try {
            artifacts(root, recipe, job.resolve("reject-wrong-api"), storage, data)
                    .resolve("component:bluetooth");
            throw new AssertionError("wrong component minimum accepted");
        } catch (Failure expected) {
            check(expected.code.equals("QINGGAN11_PHONE_PEER_ABI"), expected.code);
            negatives++;
        }

        Files.copy(
                metadata.resolve("components/bluetooth/component.json"),
                component.resolve("component.json"),
                StandardCopyOption.REPLACE_EXISTING);
        var producedMetadata =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        job.resolve(
                                                "ordinary-component/component-bluetooth/peer-component.json"))));
        check(
                Json.integer(producedMetadata, "framework_owner_classes") == 47
                        && Json.integer(producedMetadata, "phone_owner_classes") == 47,
                "both actual parent owner inventories");
        var result = new LinkedHashMap<String, Object>();
        result.put("schema", 1);
        result.put("ordinary_artifacts_resolver", true);
        result.put("shared_source_resources", 4);
        result.put("framework_compile_owner_classes", 47);
        result.put("phone_compile_owner_classes", 47);
        result.put("own_dex_classes", classes.size());
        result.put("negative_semantic_checks", negatives);
        result.put("component", output.toString());
        result.put("component_sha256", FilesSupport.hash(output));
        result.put("runtime_verified", false);
        result.put("guest_started", false);
        Files.writeString(work.resolve("peer-component-receipt.json"), Json.write(result) + "\n");
        System.out.println(Json.write(result));
    }
}
