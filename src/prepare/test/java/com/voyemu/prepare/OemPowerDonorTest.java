package com.voyemu.prepare;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.iface.ClassDef;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.TreeMap;

public final class OemPowerDonorTest {
    static final String PIN = "e46d3e5db0cf932cd3a39654a2f95620f2668ada8fb32b904c1684f936eca510",
            OTHER_PIN = "c178682be164be4872697dbb5accdc6ea28446941b6d681ddb736f2fdb627c07";

    static void check(boolean b, String message) {
        if (!b) {
            throw new AssertionError(message);
        }
    }

    static void resolver(Path root, Path work, Path key, Path cert, String id, String logical)
            throws Exception {
        var data =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve("profiles/" + id + "/profile.json"))));

        if (!logical.equals(id)) {
            data.put("id", logical);
            data.put("capture", Map.of("profile", logical, "referenceProfile", id));
        }

        var profile =
                new ProfileRegistry.Profile(
                        logical, "test", data, "voyemu/profiles/" + id + "/", root);
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
        Path sdk = root.resolve(".local/toolchains/native-sdk"),
                platform =
                        root.resolve(".local/toolchains/java-sdk/platforms/android-34/android.jar");
        var tools = new Toolchains(storage, manifest, sdk, key, cert, platform);
        var builder =
                new Artifacts(
                        new Artifacts.PatchedProfile(profile, Map.of()),
                        storage,
                        tools,
                        root.resolve("profiles/" + id),
                        work,
                        p -> {
                            throw new AssertionError("unexpected clean guest read");
                        },
                        message -> {});
        Path apk = builder.resolve("component:oem-power");
        var entries = NativeCore.entries(apk);
        check(
                entries.containsKey("classes.dex") && entries.containsKey("classes2.dex"),
                "real resolver missing donor or own code");
        var owner = NativeCore.classes(DexOperations.dex(apk, "classes.dex", 30));
        check(
                owner.size() == 8 && owner.contains("Lcom/qinggan/system/IPowerManagerService;"),
                "real resolver power donor inventory");
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 4) {
            throw new IllegalArgumentException("ROOT FRESH_WORK PLATFORM_KEY PLATFORM_CERT");
        }

        Path root = Path.of(args[0]).toRealPath(),
                work = Path.of(args[1]),
                key = Path.of(args[2]),
                cert = Path.of(args[3]);
        String p611 = TestProfiles.pinning(root, PIN), p731 = TestProfiles.pinning(root, OTHER_PIN);
        Path q611 = root.resolve(".local/profiles/" + p611 + "/originals/framework/QGAPI.jar"),
                q731 = root.resolve(".local/profiles/" + p731 + "/originals/framework/QGAPI.jar");
        var config =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve(
                                                "profiles/"
                                                        + p611
                                                        + "/components/oem-power/component.json"))));
        var recipe = Json.obj(config, "donorDex");
        var donor = DexOperations.donor(q611, recipe, work.resolve("positive611"), 30, "");
        check(donor.descriptors().size() == 8, "eight OEM power classes required");
        AudioPolicyConsumerGuard.verifyHiddenApiLayout(Files.readAllBytes(donor.dex()));
        var actual = new TreeMap<String, ClassDef>();

        for (var c : DexOperations.dex(q611, "classes.dex", 30).getClasses()) {
            if (donor.descriptors().contains(c.getType())) {
                actual.put(c.getType(), c);
            }
        }

        var other = new TreeMap<String, ClassDef>();

        for (var c : DexOperations.dex(q731, "classes.dex", 30).getClasses()) {
            if (donor.descriptors().contains(c.getType())) {
                other.put(c.getType(), c);
            }
        }

        check(
                actual.keySet().equals(donor.descriptors())
                        && other.keySet().equals(actual.keySet()),
                "exact power ABI inventory required");

        for (var c :
                DexFileFactory.loadDexFile(donor.dex().toFile(), Opcodes.forApi(30)).getClasses()) {
            check(
                    FrameworkDexMaterializer.semantic(c)
                            .equals(FrameworkDexMaterializer.semantic(actual.get(c.getType()))),
                    "donor semantic drift " + c.getType());
            check(
                    FrameworkDexMaterializer.semantic(c)
                            .equals(FrameworkDexMaterializer.semantic(other.get(c.getType()))),
                    "different 731 ABI " + c.getType());
        }

        resolver(root, work.resolve("resolver611"), key, cert, p611, p611);
        resolver(root, work.resolve("resolver731"), key, cert, p731, p731);
        resolver(root, work.resolve("resolver731alias"), key, cert, p731, "power-capture-alias");
        System.out.println(
                "611 power donor preserves eight OEM classes and hidden API layout; no guest"
                        + " acceptance");
    }
}
