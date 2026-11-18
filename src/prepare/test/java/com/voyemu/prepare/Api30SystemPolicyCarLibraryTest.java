package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.reference.FieldReference;
import org.jf.dexlib2.iface.reference.MethodReference;
import org.jf.dexlib2.iface.reference.TypeReference;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;
import java.util.regex.Pattern;

/**
 * The OEM SystemPolicy of Free A11 starts on the guest: every class and method it takes from the
 * non-APEX boot jars of the firmware resolves in the guest base or in the shared libraries its
 * prepared manifest declares, which the profile installs from the same OEM jars.
 */
public final class Api30SystemPolicyCarLibraryTest {
    static final List<String> PROFILES =
            List.of("free-11-chinese-20250710-6.11.1", "free-11-chinese-20260731-7.3.1");
    static final String RECIPE = SystemPolicyHardwareGuard.NAME + ".json";
    static final Pattern LIBRARY =
            Pattern.compile(
                    "<library name=\"([^\"]+)\" file=\"([^\"]+)\"(?: dependency=\"[^\"]+\")? />");
    static final Pattern USES =
            Pattern.compile("E: uses-library[^\\n]*\\n\\s+A: [^=]*name[^=]*=\"([^\"]+)\"");

    static void need(boolean value, String reason) {
        if (!value) {
            throw new AssertionError(reason);
        }
    }

    /** Recipe target that applies the SystemPolicy operations to the real original. */
    static final class Target implements Recipes.Target {
        final String profile, pin;
        final DexClasses classes;
        byte[] manifest;
        int guards;

        Target(String profile, String pin, DexClasses classes, byte[] manifest) {
            this.profile = profile;
            this.pin = pin;
            this.classes = classes;
            this.manifest = manifest;
        }

        @Override
        public String profile() {
            return profile;
        }

        @Override
        public int api() {
            return 30;
        }

        @Override
        public String abi() {
            return "arm64-v8a";
        }

        @Override
        public Path work() {
            return null;
        }

        @Override
        public Path output() {
            return null;
        }

        @Override
        public Map<String, Path> uses() {
            return Map.of();
        }

        @Override
        public String originalSha256() {
            return pin;
        }

        @Override
        public void addDex(String source) {
            throw new AssertionError();
        }

        @Override
        public void putEntry(Path source, String name) {
            throw new AssertionError();
        }

        @Override
        public void removeEntry(String name) {
            throw new AssertionError();
        }

        @Override
        public void useLibraries(List<String> names) throws Exception {
            manifest = ManifestLibraries.add(manifest, names);
        }

        @Override
        public void guardSystemPolicyHardware(boolean apply) {
            if (apply) {
                guards++;
                SystemPolicyHardwareGuard.apply(profile, 30, pin, classes);
            }
        }
    }

    static Map<String, ClassDef> load(Map<String, byte[]> archive) {
        Map<String, ClassDef> result = new HashMap<>();

        for (var entry : new TreeMap<>(archive).entrySet()) {
            if (entry.getKey().matches("classes[0-9]*\\.dex")) {
                for (ClassDef type :
                        new DexBackedDexFile(Opcodes.forApi(30), entry.getValue()).getClasses()) {
                    result.putIfAbsent(type.getType(), type);
                }
            }
        }

        return result;
    }

    static Map<String, ClassDef> load(List<Path> jars) throws Exception {
        Map<String, ClassDef> result = new HashMap<>();

        for (Path jar : jars) {
            load(NativeCore.entries(jar)).forEach(result::putIfAbsent);
        }

        return result;
    }

    static String element(String type) {
        String value = type.replaceFirst("^\\[+", "");

        return value.startsWith("L") ? value : null;
    }

    static String signature(Method method) {
        return method.getName()
                + "("
                + String.join("", method.getParameterTypes())
                + ")"
                + method.getReturnType();
    }

    /** External types and method references of the code of one class set. */
    record Uses(Set<String> types, Set<String> methods) {}

    static Uses uses(Map<String, ClassDef> code) {
        Set<String> types = new TreeSet<>(), methods = new TreeSet<>();

        for (ClassDef type : code.values()) {
            if (type.getSuperclass() != null) {
                types.add(type.getSuperclass());
            }

            types.addAll(type.getInterfaces());

            for (Method method : type.getMethods()) {
                if (method.getImplementation() == null) {
                    continue;
                }

                for (var instruction : method.getImplementation().getInstructions()) {
                    if (!(instruction instanceof ReferenceInstruction reference)) {
                        continue;
                    }

                    switch (reference.getReference()) {
                        case TypeReference value -> types.add(value.getType());
                        case FieldReference value -> types.add(value.getDefiningClass());
                        case MethodReference value -> {
                            types.add(value.getDefiningClass());
                            methods.add(
                                    value.getDefiningClass()
                                            + "->"
                                            + value.getName()
                                            + "("
                                            + String.join("", value.getParameterTypes())
                                            + ")"
                                            + value.getReturnType());
                        }
                        default -> {}
                    }
                }
            }
        }

        Set<String> external = new TreeSet<>();

        for (String type : types) {
            String value = element(type);

            if (value != null && !code.containsKey(value)) {
                external.add(value);
            }
        }

        methods.removeIf(m -> code.containsKey(m.substring(0, m.indexOf("->"))));

        return new Uses(external, methods);
    }

    /** True when the class set declares the method on the type or a supertype it defines. */
    static boolean declares(Map<String, ClassDef> classes, String reference) {
        String owner = reference.substring(0, reference.indexOf("->")),
                wanted = reference.substring(reference.indexOf("->") + 2);
        ArrayDeque<String> queue = new ArrayDeque<>(List.of(owner));
        Set<String> seen = new TreeSet<>();

        while (!queue.isEmpty()) {
            ClassDef type = classes.get(queue.poll());

            if (type == null || !seen.add(type.getType())) {
                continue;
            }

            for (Method method : type.getMethods()) {
                if (signature(method).equals(wanted)) {
                    return true;
                }
            }

            if (type.getSuperclass() != null) {
                queue.add(type.getSuperclass());
            }

            queue.addAll(type.getInterfaces());
        }

        return false;
    }

    /**
     * Types and methods of the code that the firmware boot jars provide and the guest classes do
     * not; APEX and core jars are the same AOSP R modules on both sides and stay out of the
     * comparison.
     */
    static List<String> gaps(
            Map<String, ClassDef> code,
            Map<String, ClassDef> firmware,
            Map<String, ClassDef> guest) {
        Uses uses = uses(code);
        List<String> result = new ArrayList<>();

        for (String type : uses.types()) {
            if (firmware.containsKey(type) && !guest.containsKey(type)) {
                result.add(type);
            }
        }

        for (String method : uses.methods()) {
            if (declares(firmware, method) && !declares(guest, method)) {
                result.add(method);
            }
        }

        return result;
    }

    /** Library name to file of the permission files the profile installs. */
    static Map<String, String> declaredLibraries(
            Path root, String profile, Map<String, Object> definition) throws Exception {
        Map<String, String> result = new TreeMap<>();
        var files = Json.obj(definition, "files");

        for (Object value : Json.list(definition.get("install"))) {
            var item = Json.object(value);
            String artifact = Json.str(item, "artifact");

            if (!(item.get("target") instanceof String target)
                    || !target.startsWith("/system/etc/permissions/")
                    || !artifact.startsWith("files:")) {
                continue;
            }

            String file = Json.str(Json.obj(files, artifact.substring(6)), "file");
            var matcher =
                    LIBRARY.matcher(
                            Files.readString(
                                    root.resolve("profiles/" + profile + "/files/" + file)));

            while (matcher.find()) {
                need(
                        result.put(matcher.group(1), matcher.group(2)) == null,
                        profile + ": library declared twice: " + matcher.group(1));
            }
        }

        return result;
    }

    /** The original that the profile installs at a guest path, directly or as a patch input. */
    static String installedOriginal(Map<String, Object> definition, String target) {
        String found = null;

        for (Object value : Json.list(definition.get("install"))) {
            var item = Json.object(value);

            if (!target.equals(item.get("target"))) {
                continue;
            }

            need(found == null, "Two installs of " + target);
            String artifact = Json.str(item, "artifact");

            if (artifact.startsWith("patched:")) {
                artifact =
                        Json.str(
                                Json.obj(Json.obj(definition, "patches"), artifact.substring(8)),
                                "input");
            }

            need(artifact.startsWith("original:"), target + " must come from an OEM original");
            found = artifact.substring(9);
        }

        need(found != null, "Nothing installed at " + target);

        return found;
    }

    static List<String> manifestLibraries(Path root, byte[] manifest) throws Exception {
        Path aapt =
                root.resolve(
                        ".local/toolchains/native-sdk/build-tools/36.1.0/aapt2"
                                + (System.getProperty("os.name").startsWith("Windows")
                                        ? ".exe"
                                        : ""));
        Path apk = Files.createTempFile("systempolicy-manifest-", ".apk");

        try {
            NativeCore.zip(apk, Map.of("AndroidManifest.xml", manifest), true);
            var process =
                    new ProcessBuilder(
                                    aapt.toString(),
                                    "dump",
                                    "xmltree",
                                    apk.toString(),
                                    "--file",
                                    "AndroidManifest.xml")
                            .redirectErrorStream(true)
                            .start();
            String xml =
                    new String(process.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
            need(process.waitFor() == 0, "Prepared manifest decode failed");
            need(
                    xml.contains("package=\"com.qinggan.systempolicy\"")
                            && xml.contains("android.uid.system")
                            && xml.contains("com.qinggan.systempolicy.SystemPolicyService"),
                    "Prepared manifest lost the OEM package/shared UID/service contract");
            List<String> result = new ArrayList<>();
            var matcher = USES.matcher(xml);

            while (matcher.find()) {
                result.add(matcher.group(1));
            }

            return result;
        } finally {
            Files.delete(apk);
        }
    }

    /** Every abstract method of each android.car listener SystemPolicy implements has a body. */
    static void implemented(
            Map<String, ClassDef> code, Map<String, ClassDef> guest, String profile) {
        int listeners = 0;

        for (ClassDef type : code.values()) {
            for (String face : type.getInterfaces()) {
                if (!face.startsWith("Landroid/car/")) {
                    continue;
                }

                listeners++;
                ClassDef declared = guest.get(face);
                need(declared != null, profile + ": interface absent " + face);

                for (Method method : declared.getMethods()) {
                    if ((method.getAccessFlags() & 0x400) == 0) {
                        continue;
                    }

                    boolean body = false;

                    for (Method own : type.getMethods()) {
                        body |=
                                own.getImplementation() != null
                                        && signature(own).equals(signature(method));
                    }

                    need(body, profile + ": " + type.getType() + " lacks " + signature(method));
                }
            }
        }

        // Three power state and three Car lifecycle listeners.
        need(listeners == 6, profile + ": SystemPolicy android.car listeners differ");
    }

    /** Inputs: SystemPolicy APK, QGAPI, QGMOD, android.car and framework jars of the firmware. */
    static void check(Path root, Path base, String profile, List<Path> inputs) throws Exception {
        Path policy = inputs.get(0),
                qgapi = inputs.get(1),
                qgmod = inputs.get(2),
                car = inputs.get(3),
                oemFramework = inputs.get(4);
        var definition =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve("profiles/" + profile + "/profile.json"))));
        var originals = Json.obj(definition, "original");
        String pin = SystemPolicyHardwareGuard.PINS.get(profile);
        Map<String, byte[]> archive = new TreeMap<>(NativeCore.entries(policy));

        NativeCore.pinned(policy, pin);
        need(
                pin.equals(Json.str(Json.obj(originals, "SystemPolicy"), "sha256")),
                profile + ": SystemPolicy pin differs from the profile");
        var patch = Json.obj(Json.obj(definition, "patches"), "SystemPolicy");
        need(
                "original:SystemPolicy".equals(patch.get("input"))
                        && Json.list(patch.get("steps")).equals(List.of(RECIPE)),
                profile + ": SystemPolicy must be the OEM original with its recipe");
        Path recipe = root.resolve("profiles/" + profile + "/patches/" + RECIPE);
        var metadata = Json.object(Json.parse(Files.readString(recipe)));
        metadata.put("sourceSha256", FilesSupport.hash(recipe));
        var classes =
                new DexClasses(
                        archive,
                        archive.keySet().stream()
                                .filter(n -> n.matches("classes[0-9]*\\.dex"))
                                .sorted()
                                .toList(),
                        30);
        var target = new Target(profile, pin, classes, archive.get("AndroidManifest.xml"));
        Recipes.apply(RECIPE, metadata, target);
        need(target.guards == 1, profile + ": SystemPolicy recipe must guard once");
        classes.write(archive);
        Map<String, ClassDef> code = load(archive);
        List<String> libraries = manifestLibraries(root, target.manifest);
        need(
                libraries.equals(SystemPolicyHardwareGuard.API30_LIBRARIES),
                profile + ": prepared SystemPolicy declares " + libraries);
        var declared = declaredLibraries(root, profile, definition);
        Map<String, Path> provided = Map.of("QGAPI", qgapi, "QGMOD", qgmod, "android.car", car);
        List<Path> guestJars = new ArrayList<>(List.of(base));

        for (String library : libraries) {
            String file = declared.get(library);
            need(file != null, profile + ": library not declared on the guest: " + library);
            String original = installedOriginal(definition, file);
            var input = Json.obj(originals, original);
            need(
                    file.equals(Json.str(Json.obj(input, "acquisition"), "path"))
                            && provided.containsKey(library),
                    profile + ": " + library + " must be the OEM jar of the same firmware path");
            NativeCore.pinned(provided.get(library), Json.str(input, "sha256"));
            guestJars.add(provided.get(library));
        }

        NativeCore.pinned(oemFramework, Json.str(Json.obj(originals, "frameworkBase"), "sha256"));
        NativeCore.pinned(qgmod, Json.str(Json.obj(originals, "QGMOD"), "sha256"));
        Map<String, ClassDef> guest = load(guestJars),
                firmware = load(List.of(oemFramework, qgapi, qgmod, car));
        List<String> found = gaps(code, firmware, guest);
        need(found.isEmpty(), profile + ": SystemPolicy references absent on the guest: " + found);
        Uses uses = uses(code);
        need(
                uses.types()
                                .containsAll(
                                        List.of(
                                                "Landroid/car/Car;",
                                                "Landroid/car/Car$CarServiceLifecycleListener;",
                                                "Landroid/car/hardware/power/CarPowerManager;",
                                                "Landroid/car/hardware/power/CarPowerManager$CarPowerStateListener;"))
                        && uses.types().stream()
                                .filter(t -> t.startsWith("Landroid/car/"))
                                .allMatch(guest::containsKey),
                profile + ": SystemPolicy android.car types must resolve from the library");
        implemented(code, guest, profile);
        // The library itself links against the guest base wherever the firmware framework did.
        Map<String, ClassDef> library = load(List.of(car));
        found = gaps(library, load(List.of(oemFramework)), load(List.of(base)));
        need(found.isEmpty(), profile + ": android.car references absent on the guest: " + found);
        // Without the OEM android.car library the start path fails exactly as on the Mac guest.
        guest = load(List.of(base, qgapi));
        found = gaps(code, firmware, guest);
        need(
                found.contains("Landroid/car/hardware/power/CarPowerManager$CarPowerStateListener;")
                        && found.stream().allMatch(g -> g.startsWith("Landroid/car/")),
                profile + ": QGAPI alone must lack exactly android.car: " + found);
    }

    public static void main(String[] args) throws Exception {
        need(args.length == 12, "ROOT, base framework.jar, five OEM inputs per Free A11 profile");
        Path root = Path.of(args[0]), base = Path.of(args[1]);
        Map<String, List<Path>> inputs = new LinkedHashMap<>();

        for (int i = 0; i < PROFILES.size(); i++) {
            List<Path> paths = new ArrayList<>();

            for (int j = 0; j < 5; j++) {
                paths.add(Path.of(args[2 + 5 * i + j]));
            }

            inputs.put(PROFILES.get(i), paths);
        }

        for (var entry : inputs.entrySet()) {
            check(root, base, entry.getKey(), entry.getValue());
        }

        System.out.println(
                "Free A11 SystemPolicy PASS: the prepared manifest declares QGAPI and android.car,"
                        + " both installed from the pinned OEM jars of the firmware path; every"
                        + " class and method SystemPolicy takes from the firmware boot jars"
                        + " resolves on the guest, its android.car listeners are implemented, the"
                        + " library links against the guest base, and QGAPI alone lacks exactly"
                        + " android.car");
    }
}
