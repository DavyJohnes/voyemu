package com.voyemu.prepare;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.reference.FieldReference;
import org.jf.dexlib2.iface.reference.MethodReference;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

/**
 * The first Voice click must receive the real OEM manager before Fragment.onResume initializes
 * views.
 */
public final class NxpVoiceInitializationTest {
    static final String NAME = "nxp-settings-voice-initialization.json";
    static final String PIN = "b3a79c9cb737b5c84256c98b9e00553467bc9fda43b975398a7284a0a9cac741";
    static final String TYPE = "Lcom/qinggan/app/setting/module/voice/presenter/VoicePresenter;",
            MANAGER = "Lcom/qinggan/speech/WakeupManager;";
    static final List<String> INSERT =
            List.of(
                    "invoke-static {}, " + MANAGER + "->getInstance()" + MANAGER,
                    "move-result-object v0",
                    "iput-object v0, p0, " + TYPE + "->mWakeupManager:" + MANAGER);

    static void need(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static final class Target implements Recipes.Target {
        String id = TestProfiles.SYNTHETIC, pin = PIN;
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

    static void reject(Target target, Map<String, Object> recipe) throws Exception {
        String before = FrameworkDexMaterializer.semantic(target.classes.get(TYPE));
        boolean rejected = false;

        try {
            Recipes.apply(NAME, recipe, target);
        } catch (Failure expected) {
            rejected = true;
        }

        need(rejected, "Unverified Voice recipe accepted");
        need(
                FrameworkDexMaterializer.semantic(target.classes.get(TYPE)).equals(before),
                "Rejected recipe mutated the class");
    }

    static List<String> constructor(ClassDef type) {
        for (var method : type.getMethods()) {
            if (method.getName().equals("<init>")) {
                var code = method.getImplementation();

                return DexEdits.render(
                        code,
                        new DexText.Frame(code.getRegisterCount(), DexEdits.parameters(method)));
            }
        }

        throw new AssertionError("constructor absent");
    }

    static Map<String, Object> copy(Map<String, Object> value) {
        return Json.object(Json.parse(Json.write(value)));
    }

    static boolean constructorInitialized(ClassDef type) {
        for (var method : type.getMethods()) {
            if (method.getName().equals("<init>")) {
                boolean manager = false, assigned = false;

                for (var instruction : method.getImplementation().getInstructions()) {
                    if (instruction instanceof ReferenceInstruction ref) {
                        if (ref.getReference() instanceof MethodReference called
                                && called.getDefiningClass().equals(MANAGER)
                                && called.getName().equals("getInstance")) {
                            manager = true;
                        }

                        if (ref.getReference() instanceof FieldReference field
                                && field.getDefiningClass().equals(TYPE)
                                && field.getName().equals("mWakeupManager")) {
                            assigned = manager;
                        }
                    }

                    if (instruction.getOpcode() == Opcode.RETURN_VOID) {
                        return assigned;
                    }
                }
            }
        }

        return false;
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), work = Path.of(args[1]);

        Files.createDirectories(work);

        if (args.length == 2) {
            Path component =
                    root.resolve(
                            "profiles/"
                                    + TestProfiles.pinning(root, PIN)
                                    + "/components/framework-qgapi/component.json");
            var donor = Json.obj(Json.object(Json.parse(Files.readString(component))), "donorDex");
            String handler = "com.qinggan.system.SystemPolicyManager$OnInitHandler";
            need(
                    Json.list(donor.get("classes")).contains(handler),
                    "QGAPI donor omits Voice SystemPolicyManager handler");
            Path qgime =
                    root.resolve(
                            ".local/profiles/"
                                    + TestProfiles.pinning(root, PIN)
                                    + "/originals/qgime.apk");
            NativeCore.pinned(qgime, Json.str(donor, "sha256"));
            var dex = DexFileFactory.loadDexContainer(qgime.toFile(), Opcodes.forApi(28));
            boolean found = false;

            for (String entry : dex.getDexEntryNames()) {
                for (var type : dex.getEntry(entry).getDexFile().getClasses()) {
                    if (type.getType()
                            .equals("Lcom/qinggan/system/SystemPolicyManager$OnInitHandler;")) {
                        found = true;
                    }
                }
            }

            need(found, "Pinned OEM donor lacks Voice SystemPolicyManager handler");
        }

        Path recipeFile =
                args.length > 2
                        ? Path.of(args[2])
                        : root.resolve(
                                "profiles/" + TestProfiles.pinning(root, PIN) + "/patches/" + NAME);
        var recipe = Json.object(Json.parse(Files.readString(recipeFile)));
        recipe.put("sourceSha256", FilesSupport.hash(recipeFile));
        Path apk =
                root.resolve(
                        ".local/profiles/"
                                + TestProfiles.pinning(root, PIN)
                                + "/originals/setting.apk");
        NativeCore.pinned(apk, PIN);
        var container = DexFileFactory.loadDexContainer(apk.toFile(), Opcodes.forApi(28));
        DexFile owner = null;
        ClassDef original = null;

        for (String entry : container.getDexEntryNames()) {
            for (var type : container.getEntry(entry).getDexFile().getClasses()) {
                if (type.getType().equals(TYPE)) {
                    owner = container.getEntry(entry).getDexFile();
                    original = type;
                }
            }
        }

        need(
                owner != null && original != null && !constructorInitialized(original),
                "Original first-click defect witness changed");
        Target target = new Target(work);
        Map<String, byte[]> archive = new java.util.TreeMap<>(NativeCore.entries(apk));
        target.classes =
                new DexClasses(
                        archive,
                        archive.keySet().stream()
                                .filter(name -> name.matches("classes[0-9]*\\.dex"))
                                .sorted()
                                .toList(),
                        28);
        target.id = TestProfiles.SYNTHETIC;
        target.pin = "0".repeat(64);
        reject(target, recipe);
        target.pin = PIN;
        target.api = 30;
        reject(target, recipe);
        target.api = 28;
        var missing = copy(recipe);
        missing.remove("sourceSha256");
        reject(target, missing);
        var wrong = copy(recipe);
        wrong.put("constants", Map.of("INPUT_SHA256", "0".repeat(64)));
        reject(target, wrong);
        var changedMethod = copy(recipe);
        var call = Json.object(Json.list(changedMethod.get("calls")).get(0));
        call.put(
                "args",
                List.of(
                        TYPE + "-><init>(Landroid/content/Context;)V",
                        Json.list(call.get("args")).get(1)));
        changedMethod.put("calls", List.of(call));
        reject(target, changedMethod);
        List<String> before = constructor(original);
        Recipes.apply(NAME, recipe, target);
        List<String> after = constructor(target.classes.get(TYPE));
        int inserted = java.util.Collections.indexOfSubList(after, INSERT);
        List<String> rest = new java.util.ArrayList<>(after);
        rest.subList(Math.max(inserted, 0), Math.max(inserted, 0) + INSERT.size()).clear();
        need(
                inserted >= 0 && rest.equals(before),
                "Edit changed anything beyond singleton initialization");
        target.classes.write(archive);
        ClassDef result = null;

        for (var type :
                new org.jf.dexlib2.dexbacked.DexBackedDexFile(
                                Opcodes.forApi(28), archive.get(target.classes.entry(TYPE)))
                        .getClasses()) {
            if (type.getType().equals(TYPE)) {
                result = type;
            }
        }

        need(
                result != null && constructorInitialized(result),
                "Serialized constructor did not initialize real manager before return");

        if (args.length == 2) {
            var profile =
                    Json.object(
                            Json.parse(
                                    Files.readString(
                                            root.resolve(
                                                    "profiles/"
                                                            + TestProfiles.pinning(root, PIN)
                                                            + "/profile.json"))));
            need(
                    Json.list(Json.obj(Json.obj(profile, "patches"), "Setting").get("steps"))
                            .contains(NAME),
                    "Settings does not use initialization recipe");
        }

        System.out.println(
                "NXP Voice initialization PASS: original defect witness, real singleton before"
                        + " return, unchanged remaining OEM code, serialized DEX, wrong"
                        + " profile/APK/API/recipe/constant/method negatives");
    }
}
