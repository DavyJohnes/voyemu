package com.voyemu.prepare;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableField;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction11x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction21s;
import org.jf.dexlib2.immutable.value.ImmutableBooleanEncodedValue;
import org.jf.dexlib2.writer.pool.DexPool;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

/**
 * Requires owner-pinned inputs. Builds the actual production Artifacts graph; never starts Android.
 */
public final class NxpSystemUiArtifactTest {
    static void need(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    static Map<String, ClassDef> classes(Path apk) throws Exception {
        var result = new TreeMap<String, ClassDef>();
        var container = DexFileFactory.loadDexContainer(apk.toFile(), Opcodes.forApi(28));

        for (String entry : container.getDexEntryNames()) {
            for (ClassDef type : container.getEntry(entry).getDexFile().getClasses()) {
                need(result.put(type.getType(), type) == null, "duplicate class " + type.getType());
            }
        }

        return result;
    }

    static void sameClasses(
            Map<String, ClassDef> expected,
            Map<String, ClassDef> actual,
            Set<String> selected,
            Path work)
            throws Exception {
        need(actual.keySet().containsAll(selected), "selected classes missing");
        var left = new ArrayList<ClassDef>();
        var right = new ArrayList<ClassDef>();

        for (String descriptor : selected) {
            need(expected.containsKey(descriptor), "donor class missing " + descriptor);
            left.add(expected.get(descriptor));
            right.add(actual.get(descriptor));
        }

        // DexPool omits default-valued static initializers; serialize both sides before comparing
        // all code and metadata.
        Files.createDirectories(work);
        Path leftDex = work.resolve("original.dex"), rightDex = work.resolve("packaged.dex");
        DexPool.writeTo(leftDex.toString(), new ImmutableDexFile(Opcodes.forApi(28), left));
        DexPool.writeTo(rightDex.toString(), new ImmutableDexFile(Opcodes.forApi(28), right));
        var original = new java.util.TreeMap<String, String>();
        var packaged = new java.util.TreeMap<String, String>();

        for (var type :
                DexFileFactory.loadDexFile(leftDex.toFile(), Opcodes.forApi(28)).getClasses()) {
            original.put(type.getType(), FrameworkDexMaterializer.semantic(type));
        }

        for (var type :
                DexFileFactory.loadDexFile(rightDex.toFile(), Opcodes.forApi(28)).getClasses()) {
            packaged.put(type.getType(), FrameworkDexMaterializer.semantic(type));
        }

        need(original.equals(packaged), "OEM class or method changed");
    }

    static ClassDef control(Boolean initial, int result) {
        String name = "Ltest/Inverse;";
        var field =
                new ImmutableField(
                        name,
                        "flag",
                        "Z",
                        9,
                        initial == null ? null : ImmutableBooleanEncodedValue.forBoolean(initial),
                        Set.of(),
                        Set.of());
        var method =
                new ImmutableMethod(
                        name,
                        "value",
                        List.of(),
                        "I",
                        9,
                        Set.of(),
                        Set.of(),
                        new ImmutableMethodImplementation(
                                1,
                                List.of(
                                        new ImmutableInstruction21s(Opcode.CONST_16, 0, result),
                                        new ImmutableInstruction11x(Opcode.RETURN, 0)),
                                List.of(),
                                List.of()));

        return new ImmutableClassDef(
                name,
                1,
                "Ljava/lang/Object;",
                List.of(),
                null,
                Set.of(),
                List.of(field),
                List.of(),
                List.of(method),
                List.of());
    }

    static void inverseControls(Path work) throws Exception {
        String name = "Ltest/Inverse;";
        var original = Map.of(name, control(false, 0));

        sameClasses(
                original,
                Map.of(name, control(null, 0)),
                Set.of(name),
                work.resolve("default-false"));

        for (int mutant = 0; mutant < 2; mutant++) {
            boolean rejected = false;

            try {
                sameClasses(
                        original,
                        Map.of(name, control(mutant == 0, mutant == 1 ? 1 : 0)),
                        Set.of(name),
                        work.resolve("mutant-" + mutant));
            } catch (AssertionError expected) {
                need(
                        expected.getMessage().equals("OEM class or method changed"),
                        "unexpected inverse failure");
                rejected = true;
            }

            need(rejected, "inverse accepted changed initializer or method");
        }
    }

    static Set<String> inventory(String component) {
        return switch (component) {
            case "media-callbacks" -> NxpSettingsLibrary.mediaTypes();
            case "settings-runtime" -> NxpSettingsLibrary.RUNTIME_TYPES;
            case "settings-managers" -> NxpSettingsLibrary.MANAGER_TYPES;
            case "systemui-runtime-a" -> NxpSettingsLibrary.SYSTEM_UI_RUNTIME_A;
            case "systemui-runtime-b" -> NxpSettingsLibrary.SYSTEM_UI_RUNTIME_B;
            case "systemui-runtime-c" -> NxpSettingsLibrary.SYSTEM_UI_RUNTIME_C;
            default -> throw new AssertionError("unknown donor component " + component);
        };
    }

    static Path pinned(Path path, String sha, Map<Path, String> tracked) throws Exception {
        need(
                path.isAbsolute()
                        && sha.matches("[a-f0-9]{64}")
                        && Files.isRegularFile(path)
                        && !Files.isSymbolicLink(path)
                        && FilesSupport.hash(path).equals(sha),
                "production input pin differs " + path);
        Storage.confined(path, ".");
        tracked.put(path, sha);

        return path;
    }

    public static void main(String[] args) throws Exception {
        need(args.length == 3, "root fresh-work pinned-production-request.json");
        Path root = Path.of(args[0]).toRealPath(), work = Path.of(args[1]);
        Files.createDirectory(work);
        inverseControls(work.resolve("inverse-controls"));
        String id = TestProfiles.pinning(root, NxpSettingsLibrary.PIN);
        var registry = new ProfileRegistry(root, id);
        var profile = registry.get(id);
        Path local = root.resolve(".local");
        var storage =
                new Storage(
                        local,
                        local.resolve("config"),
                        local.resolve("cache"),
                        local.resolve("state"),
                        local.resolve("run"),
                        local.resolve("logs"));
        var request = Json.object(Json.parse(Files.readString(Path.of(args[2]))));
        var tools = Json.obj(request, "tools");
        var tracked = new LinkedHashMap<Path, String>();
        var selected = pinnedToolchains(root, storage, tools, tracked);
        var builder =
                new Artifacts(
                        profile,
                        storage,
                        selected,
                        registry.sourceDirectory(profile),
                        work.resolve("artifacts"),
                        logical -> {
                            throw new AssertionError("Unexpected Android input " + logical);
                        },
                        System.out::println);
        Path original = builder.resolve("original:SystemUI"),
                apk = builder.resolve("patched:SystemUI");
        var old = checkApkEntries(original, apk);
        var all = classes(apk);
        var originalTypes = classes(original);
        NxpSystemUiClosureTest.controls(all, old);
        checkDonorClosure(all, originalTypes);
        checkDonorComponents(builder, all, work);
        checkLibraryScope(profile);
        need(
                new com.android.apksig.ApkVerifier.Builder(apk.toFile())
                        .build()
                        .verify()
                        .isVerified(),
                "APK signature invalid");
        registry.verifySource(profile);

        for (var pin : tracked.entrySet()) {
            need(
                    FilesSupport.hash(pin.getKey()).equals(pin.getValue()),
                    "production tool changed during artifact test " + pin.getKey());
        }

        Files.writeString(
                work.resolve("result.json"),
                Json.write(
                                Map.of(
                                        "artifact",
                                        apk.toString(),
                                        "sha256",
                                        FilesSupport.hash(apk),
                                        "donors",
                                        248,
                                        "original_entries_unchanged",
                                        true,
                                        "oem_class_inverse",
                                        true,
                                        "runtime_verified",
                                        false))
                        + "\n");
        System.out.println(
                "Actual SystemUI Artifacts APK: 248 donors, ZIP/method inverse and 12 donor"
                        + " inventory negatives PASS; runtime still required");
    }

    static Toolchains pinnedToolchains(
            Path root, Storage storage, Map<String, Object> tools, Map<Path, String> tracked)
            throws Exception {
        var signing = Json.obj(tools, "signing");
        Path sdk = Path.of(Json.str(tools, "sdk"));
        Path manifest =
                pinned(
                        root.resolve("src/app/toolchain.json"),
                        Json.str(tools, "manifest_sha256"),
                        tracked);
        var toolchain = Json.object(Json.parse(Files.readString(manifest)));

        for (var entry : Json.obj(tools, "files").entrySet()) {
            pinned(Storage.confined(sdk, entry.getKey()), (String) entry.getValue(), tracked);
        }

        Path key =
                pinned(Path.of(Json.str(signing, "key")), Json.str(signing, "key_sha256"), tracked);
        Path certificate =
                pinned(
                        Path.of(Json.str(signing, "certificate")),
                        Json.str(signing, "certificate_sha256"),
                        tracked);
        Path platform = null;

        if (tools.containsKey("platform")) {
            var specification = Json.obj(tools, "platform");
            platform =
                    pinned(
                            Path.of(Json.str(specification, "file")),
                            Json.str(specification, "sha256"),
                            tracked);
        }

        var selected = new Toolchains(storage, toolchain, sdk, key, certificate, platform);
        var required =
                new ArrayList<Path>(
                        List.of(
                                selected.platform(),
                                selected.buildTool("aapt2"),
                                selected.buildTool("zipalign")));

        for (Path tool : required) {
            need(tracked.containsKey(tool), "required production tool pin missing " + tool);
        }

        return selected;
    }

    static Map<String, byte[]> checkApkEntries(Path original, Path apk) throws Exception {
        var old = NativeCore.entries(original);
        var built = NativeCore.entries(apk);

        for (var entry : old.entrySet()) {
            if (!DexOperations.signatureEntry(entry.getKey())) {
                need(
                        Arrays.equals(entry.getValue(), built.get(entry.getKey())),
                        "original APK entry changed " + entry.getKey());
            }
        }

        var extra = new TreeSet<>(built.keySet());
        extra.removeAll(old.keySet());
        extra.removeIf(DexOperations::signatureEntry);
        need(
                extra.equals(
                        Set.of(
                                "classes2.dex",
                                "classes3.dex",
                                "classes4.dex",
                                "classes5.dex",
                                "classes6.dex",
                                "classes7.dex",
                                "lib/arm64-v8a/libqg_runtime.so")),
                "unexpected APK changes " + extra);

        return old;
    }

    static void checkDonorClosure(Map<String, ClassDef> all, Map<String, ClassDef> originalTypes) {
        var added = new TreeSet<>(all.keySet());

        added.removeAll(originalTypes.keySet());
        var expected = new TreeSet<>(NxpSettingsLibrary.mediaTypes());
        expected.addAll(NxpSettingsLibrary.RUNTIME_TYPES);
        expected.addAll(NxpSettingsLibrary.MANAGER_TYPES);
        expected.addAll(NxpSettingsLibrary.SYSTEM_UI_RUNTIME_A);
        expected.addAll(NxpSettingsLibrary.SYSTEM_UI_RUNTIME_B);
        expected.addAll(NxpSettingsLibrary.SYSTEM_UI_RUNTIME_C);
        need(expected.size() == 248 && added.equals(expected), "248-class donor closure differs");
        need(
                added.contains("Lcom/qinggan/bluetooth/profile/IBTCallback;"),
                "OEM SystemUI Bluetooth callback ABI missing");
        need(added.contains("Lcom/qinggan/os/Runtime;"), "reflective JNI initializer missing");
    }

    static void checkDonorComponents(Artifacts builder, Map<String, ClassDef> all, Path work)
            throws Exception {
        for (String component :
                List.of(
                        "media-callbacks",
                        "settings-runtime",
                        "settings-managers",
                        "systemui-runtime-a",
                        "systemui-runtime-b",
                        "systemui-runtime-c")) {
            Path jar = builder.resolve("component:" + component);
            Set<String> inventory = inventory(component);
            sameClasses(
                    classes(jar), all, inventory, work.resolve(component + "-packaging-inverse"));
            Map<String, ClassDef> source =
                    classes(
                            builder.resolve(
                                    component.equals("media-callbacks")
                                            ? "original:Launcher"
                                            : "original:Setting"));
            Set<String> oem = new TreeSet<>(inventory);
            oem.remove("Lcom/qinggan/util/Log;");
            sameClasses(source, all, oem, work.resolve(component + "-original-inverse"));
            var missing = new TreeSet<>(inventory);
            missing.remove(missing.first());

            try {
                DexOperations.donorSlot(jar, missing, work.resolve(component + "-missing"), 28);
                throw new AssertionError("missing class accepted");
            } catch (Failure expectedFailure) {
                // Rejection is the expected outcome.
            }

            var widened = new TreeSet<>(inventory);
            widened.add("Lunrelated/Class;");

            try {
                DexOperations.donorSlot(jar, widened, work.resolve(component + "-extra"), 28);
                throw new AssertionError("extra class accepted");
            } catch (Failure expectedFailure) {
                // Rejection is the expected outcome.
            }
        }
    }

    static void checkLibraryScope(ProfileRegistry.Profile profile) {
        for (Object item : Json.list(profile.data().get("install"))) {
            String artifact = Json.str(Json.object(item), "artifact");
            need(
                    !Set.of(
                                    "component:media-callbacks",
                                    "component:settings-runtime",
                                    "component:settings-managers",
                                    "component:systemui-runtime-a",
                                    "component:systemui-runtime-b",
                                    "component:systemui-runtime-c",
                                    "files:media-callback-library",
                                    "files:settings-runtime-library",
                                    "files:settings-managers-library")
                            .contains(artifact),
                    "duplicate shared-library install " + artifact);
        }

        for (String reference :
                List.of(
                        "component:media-callbacks",
                        "component:settings-runtime",
                        "component:settings-managers",
                        "component:systemui-runtime-a",
                        "component:systemui-runtime-b",
                        "component:systemui-runtime-c")) {
            Set<String> inventory = inventory(reference.substring("component:".length()));
            need(
                    !NxpSettingsLibrary.slot(profile.id(), 30, reference, inventory),
                    "API scope bypass");
        }
    }
}
