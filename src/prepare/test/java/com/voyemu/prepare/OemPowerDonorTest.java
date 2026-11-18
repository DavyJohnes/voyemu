package com.voyemu.prepare;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.iface.ClassDef;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.TreeMap;

public final class OemPowerDonorTest {
    static final String PROFILE = "free-11-chinese-20250710-6.11.1",
            OTHER = "free-11-chinese-20260731-7.3.1";

    static void check(boolean b, String message) {
        if (!b) {
            throw new AssertionError(message);
        }
    }

    static void reject(Path input, Map<String, Object> recipe, Path output, String profile)
            throws Exception {
        try {
            DexOperations.donor(input, recipe, output, 30, profile);
            throw new AssertionError("cross-profile donor accepted");
        } catch (Failure expected) {
            check(expected.code.equals("DONOR_PIN_MISMATCH"), expected.code);
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
        String p611 = PROFILE, p731 = OTHER;
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
        var donor = DexOperations.donor(q611, recipe, work.resolve("positive611"), 30, p611);
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

        reject(q731, recipe, work.resolve("reject731in611"), p611);
        reject(q611, recipe, work.resolve("reject611in731"), p731);
        reject(q611, recipe, work.resolve("reject611foreign"), "foreign-profile");
        reject(q731, recipe, work.resolve("reject731foreign"), "foreign-profile");
        resolver(root, work.resolve("resolver611"), key, cert, p611, p611);
        resolver(root, work.resolve("resolver731"), key, cert, p731, p731);
        resolver(root, work.resolve("resolver731alias"), key, cert, p731, "power-capture-alias");
        System.out.println(
                "611 power donor preserves eight OEM classes, hidden API layout and rejects mixed"
                        + " profiles; no guest acceptance");
    }
}
