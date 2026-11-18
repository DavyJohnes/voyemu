package com.voyemu.prepare;

import java.lang.reflect.Proxy;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public final class PhoneProviderTest {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1";

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
        String profile = PROFILE;
        var selected = new ProfileRegistry.Profile(profile, "draft", Map.of(), "", root);
        var sources = PhoneProviderGuard.sources(selected);

        checkSourcePins(sources);
        Path dex =
                PhoneProviderGuard.compile(
                        PROFILE,
                        PhoneProviderGuard.source(selected),
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
        Map<String, Object> metadata =
                Json.object(
                        Json.parse(
                                Files.readString(job.resolve(PhoneProviderGuard.NAME + ".json"))));
        metadata.put(
                "sourceSha256", FilesSupport.hash(job.resolve(PhoneProviderGuard.NAME + ".json")));
        var events = new ArrayList<String>();
        Recipes.Target target = recordingTarget(profile, events);
        checkRecipe(metadata, target, events);
        checkPatchMetadata(job, metadata);
        boolean actual = args.length == 4;

        if (actual) {
            checkActualConsumer(profile, Path.of(args[3]));
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

    private static void checkSourcePins(Map<String, byte[]> sources) throws Exception {
        var bad = new TreeMap<>(sources);

        bad.remove(bad.firstKey());
        reject(() -> PhoneProviderGuard.verifySources(PROFILE, bad));
        var changed = new TreeMap<>(sources);
        var bytes = changed.get(changed.firstKey()).clone();
        bytes[0] ^= 1;
        changed.put(changed.firstKey(), bytes);
        reject(() -> PhoneProviderGuard.verifySources(PROFILE, changed));
    }

    private static Recipes.Target recordingTarget(String profile, List<String> events) {
        return (Recipes.Target)
                Proxy.newProxyInstance(
                        Recipes.class.getClassLoader(),
                        new Class<?>[] {Recipes.Target.class},
                        (proxy, method, a) -> {
                            return switch (method.getName()) {
                                case "api" -> 30;
                                case "profile" -> profile;
                                case "abi" -> "arm64-v8a";
                                case "originalSha256" -> PhoneProviderGuard.pins(PROFILE).get(0);
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
        var noWindow = copy(metadata);
        noWindow.remove("windowPolicy");
        reject(() -> Recipes.validateApi30(PhoneProviderGuard.NAME + ".json", noWindow, target));
        var wrongWindow = copy(metadata);
        wrongWindow.put("windowPolicy", Map.of());
        reject(() -> Recipes.validateApi30(PhoneProviderGuard.NAME + ".json", wrongWindow, target));
        var missing = copy(metadata);
        missing.remove("helperSources");
        reject(() -> Recipes.validateApi30(PhoneProviderGuard.NAME + ".json", missing, target));
        var reordered = copy(metadata);
        var calls = new ArrayList<>(Json.list(reordered.get("calls")));
        java.util.Collections.reverse(calls);
        reordered.put("calls", calls);
        reject(() -> Recipes.validateApi30(PhoneProviderGuard.NAME + ".json", reordered, target));
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

    private static void checkActualConsumer(String profile, Path apk) throws Exception {
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
        byte[] output = PhoneProviderGuard.manifest(profile, 30, "arm64-v8a", apk, original);
        PhoneManifestProvidersTest.providerDeclarations(output);
        PhoneManifestPreserveTest.preserve(original, output);
        reject(() -> PhoneProviderGuard.manifest(profile, 28, "arm64-v8a", apk, original));
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
