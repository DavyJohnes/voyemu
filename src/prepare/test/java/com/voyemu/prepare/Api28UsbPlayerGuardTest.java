package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public final class Api28UsbPlayerGuardTest {
    interface Action {
        void run() throws Exception;
    }

    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static void reject(Action action) throws Exception {
        boolean rejected = false;

        try {
            action.run();
        } catch (Failure expected) {
            rejected = true;
        }

        check(rejected, "unregistered USB input accepted");
    }

    static Map<String, ClassDef> classes(byte[] dex) {
        var result = new TreeMap<String, ClassDef>();

        for (var type : new DexBackedDexFile(Opcodes.forApi(28), dex).getClasses()) {
            result.put(type.getType(), type);
        }

        return result;
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), work = Path.of(args[1]), platform = Path.of(args[2]);
        Path
                profile =
                        root.resolve(
                                "profiles/"
                                        + TestProfiles.declaring(root, Api28UsbPlayerGuard.NAME)),
                apk =
                        root.resolve(
                                ".local/profiles/"
                                        + TestProfiles.declaring(root, Api28UsbPlayerGuard.NAME)
                                        + "/originals/QGMedia.apk");
        var data = Json.object(Json.parse(Files.readString(profile.resolve("profile.json"))));
        var steps = Json.list(Json.obj(Json.obj(data, "patches"), "QGMedia").get("steps"));

        check(
                steps.contains(Api28UsbPlayerGuard.NAME + ".json"),
                "RED production A9 profile still has no real USB decoder route");
        var contract =
                Json.obj(
                        Json.object(
                                Json.parse(
                                        Files.readString(
                                                profile.resolve("patches/media-runtime.json")))),
                        "contract");
        var browsers = Json.obj(contract, "browser_classes");

        for (String service :
                List.of(
                        "LocalMediaBrowserService",
                        "LocalMedia1BrowserService",
                        "LocalMedia2BrowserService")) {
            check(
                    !browsers.containsKey("com.qinggan.media.usbmusic." + service),
                    "OEM USB queue is still replaced by a metadata stub");
        }

        byte[] original;

        try (var zip = new java.util.zip.ZipFile(apk.toFile())) {
            original = zip.getInputStream(zip.getEntry("classes3.dex")).readAllBytes();
        }

        byte[] patched =
                Api28UsbPlayerGuard.apply(TestProfiles.SYNTHETIC, 28, "arm64-v8a", apk, original);
        var before = classes(original);
        var after = classes(patched);
        check(before.keySet().equals(after.keySet()), "class inventory differs");
        int changed = 0;

        for (var name : before.keySet()) {
            if (!FrameworkDexMaterializer.semantic(
                            Api28UsbPlayerGuard.canonicalClass(before.get(name)))
                    .equals(
                            FrameworkDexMaterializer.semantic(
                                    Api28UsbPlayerGuard.canonicalClass(after.get(name))))) {
                check(
                        Api28UsbPlayerGuard.TARGETS.contains(name),
                        "unrelated OEM queue changed: " + name);
                changed++;
            }
        }

        check(changed > 0, "native decoder remained");

        for (var field : after.get(Api28UsbPlayerGuard.PLAYER + ";").getFields()) {
            if (field.getName().equals("mMediaPlayer")) {
                check(
                        field.getType().equals(Api28UsbPlayerGuard.HELPER),
                        "native decoder retained");
            }
        }

        reject(() ->
                Api28UsbPlayerGuard.apply(TestProfiles.SYNTHETIC, 30, "arm64-v8a", apk, original));
        reject(() ->
                Api28UsbPlayerGuard.apply(
                        TestProfiles.SYNTHETIC, 28, "armeabi-v7a", apk, original));
        reject(() ->
                Api28UsbPlayerGuard.apply(TestProfiles.SYNTHETIC, 28, "arm64-v8a", apk, patched));
        var archive = new java.util.TreeMap<>(NativeCore.entries(apk));
        var decoded =
                new DexClasses(
                        archive,
                        archive.keySet().stream()
                                .filter(name -> name.matches("classes[0-9]*\\.dex"))
                                .sorted()
                                .toList(),
                        28);
        Api28UsbPlayerGuard.adaptClasses(TestProfiles.SYNTHETIC, 28, "arm64-v8a", apk, decoded);
        var calls = new java.util.ArrayList<String>();

        for (var method : decoded.get(Api28UsbPlayerGuard.PLAYER + ";").getMethods()) {
            if (method.getImplementation() != null) {
                for (var instruction : method.getImplementation().getInstructions()) {
                    if (instruction
                            instanceof org.jf.dexlib2.iface.instruction.ReferenceInstruction ref) {
                        calls.add(ref.getReference().toString());
                    }
                }
            }
        }

        check(
                calls.stream()
                                .anyMatch(call ->
                                        call.startsWith(
                                                Api28UsbPlayerGuard.LOG_HELPER + "->getLog"))
                        && calls.stream()
                                .noneMatch(call ->
                                        call.startsWith(
                                                Api28UsbPlayerGuard.LOG_FACTORY + "->getLog")),
                "Player still calls throwing commons LogFactory stub");
        reject(() ->
                Api28UsbPlayerGuard.adaptClasses(
                        TestProfiles.SYNTHETIC, 28, "arm64-v8a", apk, decoded));
        Path source = profile.resolve("patches/" + Api28UsbPlayerGuard.NAME + "/src");
        Path helper =
                Api28UsbPlayerGuard.compileHelper(
                        TestProfiles.SYNTHETIC,
                        28,
                        "arm64-v8a",
                        apk,
                        source,
                        platform,
                        work.resolve("helper"));
        var helperClasses = classes(Files.readAllBytes(helper));
        check(helperClasses.containsKey(Api28UsbPlayerGuard.HELPER), "helper missing");
        check(helperClasses.containsKey(Api28UsbPlayerGuard.LOG_HELPER), "logger helper missing");
        check(
                helperClasses.keySet().stream().noneMatch(n -> n.startsWith("Lorg/ijkplayer/")),
                "compile-only signatures leaked");
        check(
                helperClasses
                        .get(Api28UsbPlayerGuard.HELPER)
                        .getSuperclass()
                        .equals("Lorg/ijkplayer/AbstractMediaPlayer;"),
                "OEM listener contract lost");
        System.out.println(
                "PASS API28 real OEM USB queue retained, native decoder references replaced,"
                        + " inverse exact, source guard, profile route, helper compiled; changed="
                        + changed
                        + " helper="
                        + FilesSupport.hash(helper));
    }
}
