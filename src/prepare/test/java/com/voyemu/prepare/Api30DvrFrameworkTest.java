package com.voyemu.prepare;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeSet;
import java.util.zip.ZipFile;

/** The OEM DVR keeps its own skin application; its superclass needs the real QGAPI loader. */
public final class Api30DvrFrameworkTest {
    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static void rejectCameraPatch(
            Map<String, Object> constants, int api, String abi, Path apk, byte[] dex)
            throws Exception {
        try {
            DvrCameraVirtualGuard.patch(constants, api, abi, apk, dex, true);
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError("Unsupported or already patched OEM DVR DEX accepted");
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]).toRealPath();
        String id = "free-11-chinese-20260731-7.3.1";
        Path work = Path.of(args[1]);
        Files.createDirectories(work);
        Path metadata = args.length == 3 ? Path.of(args[2]) : root.resolve("profiles/" + id);
        var profile = Json.object(Json.parse(Files.readString(metadata.resolve("profile.json"))));
        var patch = Json.obj(Json.obj(profile, "patches"), "Dvr");
        check(
                Json.list(patch.get("steps"))
                        .equals(
                                List.of(
                                        "api30-dvr-arm64.json",
                                        "api30-framework-libraries.json",
                                        "api30-dvr-camera-virtual.json")),
                "Production DVR must restore its classloader after guarded native preparation");
        check(
                Json.list(patch.get("uses")).equals(List.of("component:dvr-arm64-libraries")),
                "Preserve native component");
        Path originals = root.resolve(".local/profiles/" + id + "/originals");
        Path apk = originals.resolve("Dvr.apk");
        var cameraRecipe = metadata.resolve("patches/api30-dvr-camera-virtual.json");
        var cameraConstants =
                Json.obj(Json.object(Json.parse(Files.readString(cameraRecipe))), "constants");
        byte[] originalDex;
        try (var zip = new java.util.zip.ZipFile(apk.toFile())) {
            originalDex = zip.getInputStream(zip.getEntry("classes2.dex")).readAllBytes();
        }
        byte[] cameraAdapted =
                DvrCameraVirtualGuard.patch(
                        cameraConstants, 30, "arm64-v8a", apk, originalDex, true);
        check(!java.util.Arrays.equals(originalDex, cameraAdapted), "DVR hooks must change DEX");
        rejectCameraPatch(cameraConstants, 28, "arm64-v8a", apk, originalDex);
        rejectCameraPatch(cameraConstants, 30, "armeabi-v7a", apk, originalDex);
        rejectCameraPatch(cameraConstants, 30, "arm64-v8a", apk, cameraAdapted);
        var definitions = new HashMap<String, TreeSet<String>>();
        var references = new TreeSet<String>();
        boolean application = false;
        for (String name : List.of("Dvr", "QGAPI", "QGMOD", "QGVehicle")) {
            Path input = name.equals("Dvr") ? apk : originals.resolve("framework/" + name + ".jar");
            var source = Json.obj(Json.obj(profile, "original"), name);
            check(
                    FilesSupport.hash(input).equals(Json.str(source, "sha256")),
                    "Exact OEM original " + name);
            var classes = new TreeSet<String>();
            var container = DexFileFactory.loadDexContainer(input.toFile(), Opcodes.forApi(30));
            for (String entry : container.getDexEntryNames()) {
                var dex = container.getEntry(entry).getDexFile();
                for (var type : dex.getClasses()) {
                    classes.add(type.getType());
                    if (name.equals("Dvr")
                            && type.getType().equals("Lcom/qinggan/theme/SkinQGApplication;")) {
                        check(
                                entry.equals("classes2.dex")
                                        && "Lcom/qinggan/app/QGApplication;"
                                                .equals(type.getSuperclass()),
                                "Keep the actual OEM secondary DEX application");
                        application = true;
                    }
                }
                if (name.equals("Dvr")) {
                    for (var reference : dex.getTypeReferences()) {
                        references.add(reference.getType());
                    }
                }
            }
            definitions.put(name, classes);
        }

        check(application, "OEM skin application must exist");
        references.removeAll(definitions.get("Dvr"));
        var owners = new ArrayList<String>();

        for (String name : List.of("QGAPI", "QGMOD", "QGVehicle")) {
            var used = new TreeSet<>(references);
            used.retainAll(definitions.get(name));
            if (!used.isEmpty()) {
                owners.add(name);
            }
        }
        check(
                owners.equals(List.of("QGAPI")),
                "Only actual direct OEM dependencies may be exposed");
        Path recipe = metadata.resolve("patches/api30-framework-libraries.json");
        var data = Json.object(Json.parse(Files.readString(recipe)));
        data.put("sourceSha256", FilesSupport.hash(recipe));
        var target = new FrameworkLibrariesTest.Target();
        target.profile = id;
        target.pin = FilesSupport.hash(apk);
        Recipes.apply("api30-framework-libraries.json", data, target);
        check(
                target.mutations == 1 && target.installed.equals(owners),
                "Production recipe must expose exactly QGAPI");
        Path source = work.resolve("native-source");
        check(
                OemArm64LibrariesTest.stageInputs(
                        root.resolve(
                                "profiles/"
                                        + id
                                        + "/components/dvr-arm64-libraries/component.json"),
                        root.resolve(".local/toolchains/downloads/sources"),
                        source),
                "Pinned native library archives required");
        Files.createDirectories(work.resolve("native-output"));
        Path component =
                GalleryArm64Libraries.build(30, "arm64-v8a", source, work.resolve("native-output"));
        var nativeRecipe =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        metadata.resolve("patches/api30-dvr-arm64.json"))));
        var constants = Json.obj(nativeRecipe, "constants");
        var baseline = NativeCore.entries(apk);
        baseline.keySet().removeIf(DexOperations::signatureEntry);
        var nativeEntries =
                GalleryArm64Libraries.add(
                        constants, 30, "arm64-v8a", apk, baseline, component, source);
        byte[] finalManifest =
                ManifestLibraries.add(nativeEntries.get("AndroidManifest.xml"), target.installed);
        for (var entry : baseline.entrySet()) {
            check(
                    java.util.Arrays.equals(entry.getValue(), nativeEntries.get(entry.getKey())),
                    "Native step must preserve every original entry: " + entry.getKey());
        }
        var manifestFirst = new java.util.TreeMap<>(baseline);
        manifestFirst.put(
                "AndroidManifest.xml",
                ManifestLibraries.add(baseline.get("AndroidManifest.xml"), target.installed));
        boolean wrongOrderDenied = false;
        try {
            GalleryArm64Libraries.add(
                    constants, 30, "arm64-v8a", apk, manifestFirst, component, source);
        } catch (Failure expected) {
            wrongOrderDenied = expected.code.equals("GALLERY_ARM64");
        }
        check(
                wrongOrderDenied,
                "The manifest-first defect must remain rejected by the exact original-entry guard");
        try (var zip = new ZipFile(apk.toFile())) {
            byte[] manifest =
                    zip.getInputStream(zip.getEntry("AndroidManifest.xml")).readAllBytes();
            byte[] output = finalManifest;
            // This operation only receives the manifest: classes2.dex, native entries, and assets
            // cannot be replaced. The existing optional libraries must survive unchanged.
            Path aapt =
                    root.resolve(
                            ".local/toolchains/native-sdk/build-tools/36.1.0/aapt2"
                                    + (System.getProperty("os.name").startsWith("Windows")
                                            ? ".exe"
                                            : ""));
            var before = manifestText(aapt, manifest);
            var after = manifestText(aapt, output);
            for (String optional :
                    List.of("androidx.window.extensions", "androidx.window.sidecar")) {
                check(
                        before.contains(optional) && after.contains(optional),
                        "Preserve optional OEM library " + optional);
            }
            check(
                    !before.contains("\"QGAPI\"") && after.contains("\"QGAPI\""),
                    "Restore absent superclass loader edge");
            check(
                    !after.contains("\"QGMOD\"") && !after.contains("\"QGVehicle\""),
                    "No unnecessary libraries");
            boolean duplicate = false;
            try {
                ManifestLibraries.add(output, List.of("QGAPI"));
            } catch (Failure expected) {
                duplicate = true;
            }
            check(duplicate, "A second mutation must fail");
        }
        var wrong = new FrameworkLibrariesTest.Target();
        wrong.pin = "0".repeat(64);
        FrameworkLibrariesTest.reject(data, wrong);
        var changed = new HashMap<String, Object>(data);
        changed.put(
                "calls",
                List.of(
                        Map.of(
                                "operation",
                                "useLibraries",
                                "args",
                                List.of(List.of("QGAPI", "QGMOD")))));
        wrong = new FrameworkLibrariesTest.Target();
        wrong.pin = target.pin;
        FrameworkLibrariesTest.reject(changed, wrong);
        System.out.println(
                "OEM DVR QGAPI loader edge, secondary DEX ownership and optional libraries"
                        + " verified");
    }
    static String manifestText(Path aapt, byte[] manifest) throws Exception {
        Path temporary = Files.createTempFile("dvr-manifest-", ".apk");

        try {
            try (var zip = new java.util.zip.ZipOutputStream(Files.newOutputStream(temporary))) {
                zip.putNextEntry(new java.util.zip.ZipEntry("AndroidManifest.xml"));
                zip.write(manifest);
                zip.closeEntry();
            }

            var process =
                    new ProcessBuilder(
                                    aapt.toString(),
                                    "dump",
                                    "xmltree",
                                    temporary.toString(),
                                    "--file",
                                    "AndroidManifest.xml")
                            .redirectErrorStream(true)
                            .start();
            String result =
                    new String(
                            process.getInputStream().readAllBytes(),
                            java.nio.charset.StandardCharsets.UTF_8);
            check(process.waitFor() == 0, "Actual binary Android manifest must remain readable");

            return result;
        } finally {
            Files.delete(temporary);
        }
    }
}
