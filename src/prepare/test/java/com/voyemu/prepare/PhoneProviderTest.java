package com.voyemu.prepare;

import java.lang.reflect.Proxy;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public final class PhoneProviderTest {
    private static int checks;

    private static void need(boolean value) {
        checks++;

        if (!value) {
            throw new AssertionError("contract " + checks);
        }
    }

    private static void reject(Checked action) throws Exception {
        try {
            action.run();
            throw new AssertionError("accepted negative contract");
        } catch (Failure expected) {
            checks++;
        }
    }

    interface Checked {
        void run() throws Exception;
    }

    @SuppressWarnings("unchecked")
    private static Map<String, Object> copy(Map<String, Object> source) {
        return Json.object(Json.parse(Json.write(source)));
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), job = Path.of(args[1]), platform = Path.of(args[2]);
        Path step = job.resolve(PhoneProviderGuard.NAME + ".json");
        Map<String, Object> metadata = Json.object(Json.parse(Files.readString(step)));

        metadata.put("sourceSha256", FilesSupport.hash(step));
        Map<String, Object> constants = Json.obj(metadata, "constants");
        var selected =
                new ProfileRegistry.Profile(TestProfiles.SYNTHETIC, "draft", Map.of(), "", root);
        var sources = PhoneProviderGuard.sources(selected, constants);

        checkSourcePins(constants, sources);
        Path dex =
                PhoneProviderGuard.compile(
                        PhoneProviderGuard.source(selected, constants),
                        platform,
                        job.resolve("provider-compile"));
        var packaged = new java.util.TreeSet<String>();

        for (var f : FilesSupport.tree(dex).values()) {
            packaged.addAll(
                    NativeCore.classes(
                            org.jf.dexlib2.DexFileFactory.loadDexFile(
                                    f.toFile(), org.jf.dexlib2.Opcodes.forApi(30))));
        }

        need(
                packaged.contains("Lcom/voyemu/input/PhoneControlProvider;")
                        && packaged.contains("Lcom/voyemu/input/PhonePeerProvider;"));
        var events = new ArrayList<String>();
        Recipes.Target target = recordingTarget(constants, events);
        checkRecipe(metadata, target, events);
        checkPatchMetadata(job, metadata);
        boolean actual = args.length == 4;

        if (actual) {
            checkActualConsumer(constants, Path.of(args[3]));
        }

        var receipt =
                Map.of(
                        "checks",
                        checks,
                        "semantic_contract_checked",
                        actual,
                        "acquisition_integrity_separate",
                        true,
                        "provider_classes",
                        packaged.size(),
                        "normal_dependencies",
                        true,
                        "canonical_mutations",
                        false,
                        "actual_phone_manifest_verified",
                        actual,
                        "runtime_verified",
                        false);
        Files.writeString(job.resolve("producer-test-receipt.json"), Json.write(receipt) + "\n");
        System.out.println(Json.write(receipt));
    }

    private static void checkSourcePins(Map<String, Object> constants, Map<String, byte[]> sources)
            throws Exception {
        var bad = new TreeMap<>(sources);

        bad.remove(bad.firstKey());
        reject(() -> PhoneProviderGuard.verifySources(constants, bad));
        var changed = new TreeMap<>(sources);
        var bytes = changed.get(changed.firstKey()).clone();
        bytes[0] ^= 1;
        changed.put(changed.firstKey(), bytes);
        reject(() -> PhoneProviderGuard.verifySources(constants, changed));
    }

    private static Recipes.Target recordingTarget(
            Map<String, Object> constants, List<String> events) {
        return (Recipes.Target)
                Proxy.newProxyInstance(
                        Recipes.class.getClassLoader(),
                        new Class<?>[] {Recipes.Target.class},
                        (proxy, method, a) -> {
                            return switch (method.getName()) {
                                case "api" -> 30;
                                case "profile" -> TestProfiles.SYNTHETIC;
                                case "abi" -> "arm64-v8a";
                                case "originalSha256" -> StepConstants.pins(constants).get(0);
                                case "guardPhoneProvider" -> {
                                    events.add("guard:" + a[0]);
                                    yield null;
                                }
                                case "useLibraries" -> {
                                    events.add("libraries:" + a[0]);
                                    yield null;
                                }
                                default ->
                                        throw new AssertionError(
                                                "Unexpected producer operation "
                                                        + method.getName());
                            };
                        });
    }

    private static void checkRecipe(
            Map<String, Object> metadata, Recipes.Target target, List<String> events)
            throws Exception {
        Recipes.validateApi30(PhoneProviderGuard.NAME + ".json", metadata, target);
        need(events.equals(List.of("guard:false")));
        events.clear();
        Recipes.apply(PhoneProviderGuard.NAME + ".json", metadata, target);
        need(
                events.equals(
                        List.of(
                                "guard:false",
                                "guard:true",
                                "libraries:[QGAPI, QGMOD, QGVehicle]")));
        var noWindow = withConstants(metadata, constants -> constants.remove("windowPolicy"));
        reject(() -> Recipes.validateApi30(PhoneProviderGuard.NAME + ".json", noWindow, target));
        var wrongWindow =
                withConstants(metadata, constants -> constants.put("windowPolicy", Map.of()));
        reject(() -> Recipes.validateApi30(PhoneProviderGuard.NAME + ".json", wrongWindow, target));
        var missing = withConstants(metadata, constants -> constants.remove("helperSources"));
        reject(() -> Recipes.validateApi30(PhoneProviderGuard.NAME + ".json", missing, target));
        var reordered = copy(metadata);
        var calls = new ArrayList<>(Json.list(reordered.get("calls")));
        java.util.Collections.reverse(calls);
        reordered.put("calls", calls);
        reject(() -> Recipes.validateApi30(PhoneProviderGuard.NAME + ".json", reordered, target));
    }

    // A copy of the recipe whose constants differ by change.
    private static Map<String, Object> withConstants(
            Map<String, Object> metadata, java.util.function.Consumer<Map<String, Object>> change) {
        var result = copy(metadata);
        var constants = Json.obj(result, "constants");

        change.accept(constants);
        result.put("constants", constants);

        return result;
    }

    private static void checkPatchMetadata(Path job, Map<String, Object> metadata)
            throws Exception {
        Path fixture = job.resolve("metadata-fixture");

        Files.createDirectories(fixture.resolve("profiles/draft/patches"));
        String resource = "voyemu/profiles/draft/";
        Map<String, Object> data =
                Map.of(
                        "android",
                        Map.of("package", "system-images;android-30;default;arm64-v8a"),
                        "patches",
                        Map.of("Phone", Map.of("steps", List.of("phone.json"))));
        var fixtureProfile = new ProfileRegistry.Profile("draft", "draft", data, resource, fixture);
        Path recipe = fixture.resolve("profiles/draft/patches/phone.json");
        Map<String, Object> canonical = copy(metadata);
        canonical.remove("sourceSha256");
        Files.writeString(recipe, Json.write(canonical));
        need(
                Json.integer(
                                Json.object(
                                        ProfileRegistry.patchMetadata(fixtureProfile)
                                                .get("phone.json")),
                                "api")
                        == 30);
        canonical.put("api", 28);
        Files.writeString(recipe, Json.write(canonical));
        reject(() -> ProfileRegistry.patchMetadata(fixtureProfile));
        canonical.put("api", 30);
        canonical.put("sourceSha256", "0".repeat(64));
        Files.writeString(recipe, Json.write(canonical));
        reject(() -> ProfileRegistry.patchMetadata(fixtureProfile));
    }

    private static void checkActualConsumer(Map<String, Object> constants, Path apk)
            throws Exception {
        var ownerClasses = PhoneConsumerContract.read(apk);

        PhoneConsumerContract.verify(ownerClasses);

        for (String owner :
                List.of(
                        "Lcom/qinggan/bluetooth/BTManager;",
                        "Lcom/qinggan/bluetooth/BTAdapterFactory;",
                        "Lcom/qinggan/bluetoothphone/logic/manager/CacheCallDataManager;",
                        "Lcom/qinggan/bluetoothphone/logic/manager/HeadSetProfileManager$HeadSetProfile$2;")) {
            var absent = new TreeMap<>(ownerClasses);
            absent.remove(owner);
            reject(() -> PhoneConsumerContract.verify(absent));
        }

        for (String[] mutation :
                new String[][] {
                    {"Lcom/qinggan/bluetooth/BTManager;", "getFields", "mAdapter"},
                    {"Lcom/qinggan/bluetooth/BTManager;", "getMethods", "getInstance"},
                    {"Lcom/qinggan/bluetooth/BTAdapterFactory;", "getMethods", "getBTAdapter"},
                    {
                        "Lcom/qinggan/bluetoothphone/logic/manager/CacheCallDataManager;",
                        "getFields",
                        "isCallLogInserting"
                    }
                }) {
            var altered = new TreeMap<>(ownerClasses);
            var originalType = ownerClasses.get(mutation[0]);
            var masked = masked(originalType, mutation);
            altered.put(mutation[0], masked);
            reject(() -> PhoneConsumerContract.verify(altered));
        }

        byte[] original = NativeCore.entries(apk).get("AndroidManifest.xml");
        byte[] output = PhoneProviderGuard.manifest(constants, 30, "arm64-v8a", apk, original);
        PhoneManifestProvidersTest.providerDeclarations(output);
        PhoneManifestPreserveTest.preserve(original, output);
        reject(() -> PhoneProviderGuard.manifest(constants, 28, "arm64-v8a", apk, original));
    }

    private static org.jf.dexlib2.iface.ClassDef masked(
            org.jf.dexlib2.iface.ClassDef originalType, String[] mutation) {
        return (org.jf.dexlib2.iface.ClassDef)
                Proxy.newProxyInstance(
                        Recipes.class.getClassLoader(),
                        new Class<?>[] {org.jf.dexlib2.iface.ClassDef.class},
                        (proxy, member, params) -> {
                            if (member.getName().equals(mutation[1])) {
                                var members = new ArrayList<Object>();

                                for (Object value :
                                        (Iterable<?>) member.invoke(originalType, params)) {
                                    String name =
                                            value instanceof org.jf.dexlib2.iface.Method m
                                                    ? m.getName()
                                                    : ((org.jf.dexlib2.iface.Field) value)
                                                            .getName();

                                    if (!name.equals(mutation[2])) {
                                        members.add(value);
                                    }
                                }

                                return members;
                            }

                            return member.invoke(originalType, params);
                        });
    }
}
