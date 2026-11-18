package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/** OEM Bluetooth hardware policies are optional only when the real adapter is absent. */
public final class NxpSystemPolicyHardwareTest {
    static final String ID = "free-09-chinese-20221230-nxp-rc45",
            PIN = "c4920cc36a257755335c6fae098172e30414711e9f2adf3fab6dbc566edabd15",
            NAME = "nxp-systempolicy-hardware.json";
    static final List<String> TYPES =
            List.of(
                    "Lcom/qinggan/systempolicy/PolicyManager;",
                    "Lcom/qinggan/systempolicy/SystemPolicyService;");

    static void need(boolean x, String message) {
        if (!x) {
            throw new AssertionError(message);
        }
    }

    static final class Target implements Recipes.Target {
        String id = ID, pin = PIN;
        int api = 28;
        final Path work;
        DexClasses classes;

        Target(Path work) {
            this.work = work;
        }

        @Override
        public DexClasses dex() {
            return classes;
        }

        @Override
        public String profile() {
            return id;
        }

        @Override
        public String originalSha256() {
            return pin;
        }

        @Override
        public int api() {
            return api;
        }

        @Override
        public String abi() {
            return "arm64-v8a";
        }

        @Override
        public Path work() {
            return work;
        }

        @Override
        public Path output() {
            throw new AssertionError();
        }

        @Override
        public Map<String, Path> uses() {
            return Map.of();
        }

        @Override
        public void addDex(String x) {
            throw new AssertionError();
        }

        @Override
        public void putEntry(Path p, String x) {
            throw new AssertionError();
        }

        @Override
        public void removeEntry(String x) {
            throw new AssertionError();
        }

        @Override
        public void useLibraries(List<String> x) {
            throw new AssertionError();
        }
    }

    static Map<String, Object> copy(Map<String, Object> x) {
        return Json.object(Json.parse(Json.write(x)));
    }

    static DexClasses classes(Path apk) throws Exception {
        Map<String, byte[]> archive = new TreeMap<>(NativeCore.entries(apk));

        return new DexClasses(
                archive,
                archive.keySet().stream()
                        .filter(n -> n.matches("classes[0-9]*\\.dex"))
                        .sorted()
                        .toList(),
                28);
    }

    /** The instructions of every method of the class as smali lines, in method order. */
    static Map<String, List<String>> lines(DexClasses classes, String type) {
        Map<String, List<String>> result = new TreeMap<>();

        for (var method : classes.get(type).getMethods()) {
            var code = method.getImplementation();

            if (code != null) {
                result.put(
                        DexEdits.descriptor(method),
                        DexEdits.render(
                                code,
                                new DexText.Frame(
                                        code.getRegisterCount(), DexEdits.parameters(method))));
            }
        }

        return result;
    }

    static void reject(Target target, Map<String, Object> recipe) throws Exception {
        var before = new TreeMap<String, Object>();

        for (String type : TYPES) {
            before.put(type, lines(target.classes, type));
        }

        boolean rejected = false;

        try {
            Recipes.apply(NAME, recipe, target);
        } catch (Failure error) {
            rejected = true;
        }

        need(rejected, "Invalid hardware patch accepted");

        for (String type : TYPES) {
            need(
                    lines(target.classes, type).equals(before.get(type)),
                    "Rejected patch mutated OEM code");
        }
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), work = Path.of(args[1]);

        Files.createDirectories(work);
        Path apk = root.resolve(".local/profiles/" + ID + "/originals/systempolicy.apk");
        NativeCore.pinned(apk, PIN);
        Path recipePath = root.resolve("profiles/" + ID + "/patches/" + NAME);
        var recipe = Json.object(Json.parse(Files.readString(recipePath)));
        recipe.put("sourceSha256", FilesSupport.hash(recipePath));
        Target target = new Target(work);
        target.classes = classes(apk);
        target.id = "free-09-chinese-20260106-6.0.3";
        reject(target, recipe);
        target.id = ID;
        target.api = 30;
        reject(target, recipe);
        target.api = 28;
        target.pin = "0".repeat(64);
        reject(target, recipe);
        target.pin = PIN;
        var wrong = copy(recipe);
        wrong.remove("sourceSha256");
        reject(target, wrong);
        wrong = copy(recipe);
        wrong.put("constants", Map.of("INPUT_SHA256", "0".repeat(64)));
        reject(target, wrong);
        // Changed OEM code: an anchor instruction of the second guard no longer matches.
        String build = "sget-object v0, Landroid/os/Build;->";
        target.classes.edit(
                List.of(
                        new DexEdits.MethodEdit(
                                TYPES.get(0) + "-><init>(Landroid/content/Context;)V",
                                null,
                                List.of(
                                        DexEdits.Edit.replace(
                                                List.of(build + "PRODUCT:Ljava/lang/String;"),
                                                List.of(build + "MODEL:Ljava/lang/String;"))))));
        reject(target, recipe);
        target.classes = classes(apk);
        var original = new TreeMap<String, Map<String, List<String>>>();

        for (String type : TYPES) {
            original.put(type, lines(target.classes, type));
        }

        Recipes.apply(NAME, recipe, target);
        Map<String, Integer> inserted = new TreeMap<>();

        for (Object value : Json.list(recipe.get("calls"))) {
            var edit = DexEdits.MethodEdit.parse(Json.list(Json.object(value).get("args")));

            for (var change : edit.edits()) {
                inserted.merge(
                        edit.method(),
                        (int) change.insert().stream().filter(l -> !DexText.isLabel(l)).count(),
                        Integer::sum);
            }
        }

        for (String type : TYPES) {
            for (var entry : lines(target.classes, type).entrySet()) {
                // Branch targets are code addresses: compare the instructions without them.
                List<String> patched =
                        entry.getValue().stream()
                                .map(l -> l.replaceAll(":[a-z]+_[0-9a-f]+$", ""))
                                .toList();
                List<String> before =
                        original.get(type).get(entry.getKey()).stream()
                                .map(l -> l.replaceAll(":[a-z]+_[0-9a-f]+$", ""))
                                .toList();
                int matched = 0;

                for (String line : patched) {
                    if (matched < before.size() && before.get(matched).equals(line)) {
                        matched++;
                    }
                }

                need(
                        matched == before.size()
                                && patched.size() - before.size()
                                        == inserted.getOrDefault(entry.getKey(), 0),
                        "Changes beyond two capability guards");
            }
        }

        List<String> manager =
                lines(target.classes, TYPES.get(0))
                        .get(TYPES.get(0) + "-><init>(Landroid/content/Context;)V");
        int guard = -1, constructor = -1, target2 = -1;

        for (int i = 0; i < manager.size(); i++) {
            if (manager.get(i).startsWith("if-eqz v0, :cond_") && guard < 0) {
                guard = i;
            }

            if (manager.get(i)
                    .equals(
                            "new-instance v1,"
                                    + " Lcom/qinggan/systempolicy/bluetooth/BluetoothPolicy;")) {
                constructor = i;
            }

            if (manager.get(i).startsWith(build + "PRODUCT")) {
                target2 = i;
            }
        }

        need(guard >= 0 && guard < constructor, "Missing adapter must skip constructor");
        need(
                manager.get(guard)
                        .endsWith(":cond_" + Integer.toHexString(address(target.classes, target2))),
                "Branch target must follow full BT insertion");
        Map<String, byte[]> archive = new TreeMap<>(NativeCore.entries(apk));
        target.classes.write(archive);
        need(
                new org.jf.dexlib2.dexbacked.DexBackedDexFile(
                                        org.jf.dexlib2.Opcodes.forApi(28),
                                        archive.get("classes.dex"))
                                .getClasses()
                                .size()
                        > 2,
                "Real guarded DEX must serialize both classes");
        System.out.println(
                "NXP SystemPolicy hardware PASS: exact OEM two-method insertion, non-null path/all"
                        + " other code preserved, real serialized DEX, wrong"
                        + " profile/API/APK/recipe/code rejected");
    }

    /** The code address of the instruction at an index of PolicyManager.<init>. */
    static int address(DexClasses classes, int index) {
        for (var method : classes.get(TYPES.get(0)).getMethods()) {
            if (method.getName().equals("<init>")) {
                int address = 0, i = 0;

                for (var instruction : method.getImplementation().getInstructions()) {
                    if (i++ == index) {
                        return address;
                    }

                    address += instruction.getCodeUnits();
                }
            }
        }

        throw new AssertionError("instruction absent");
    }
}
