package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

/**
 * Verifies the HandlerThread relocation of OEM PhoneConnectMgr observers and its mutation guards.
 */
public final class NxpPhoneConnectAnrTest {
    static final String NAME = "nxp-phone-connect-anr.json",
            PIN = "619186d34262ea53e5386abb52ca11d74636da2a85434fafa19df7efecca565b";
    static final String TYPE = "Lcom/qinggan/speech/adapter/phoneconnect/PhoneConnectMgr;";
    static final List<String> ORIGINAL =
            List.of(
                    "new-instance v0, Landroid/os/Handler;",
                    "invoke-direct {v0}, Landroid/os/Handler;-><init>()V",
                    "const-string/jumbo v1, \"setting_receive_display_message\"",
                    "return-void");
    static final List<String> PATCHED =
            List.of(
                    "new-instance v0, Landroid/os/HandlerThread;",
                    "const-string v2, \"PChatAutoMgr\"",
                    "invoke-direct {v0, v2},"
                            + " Landroid/os/HandlerThread;-><init>(Ljava/lang/String;)V",
                    "invoke-virtual {v0}, Landroid/os/HandlerThread;->start()V",
                    "invoke-virtual {v0},"
                            + " Landroid/os/HandlerThread;->getLooper()Landroid/os/Looper;",
                    "move-result-object v2",
                    "new-instance v3, Landroid/os/Handler;",
                    "invoke-direct {v3, v2}, Landroid/os/Handler;-><init>(Landroid/os/Looper;)V",
                    "move-object v0, v3",
                    "const-string/jumbo v1, \"setting_receive_display_message\"",
                    "return-void");

    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static final class Target implements Recipes.Target {
        Path work;
        DexClasses classes;
        String profile = TestProfiles.SYNTHETIC;
        String pin = PIN;
        String abi = "arm64-v8a";
        int api = 28;

        @Override
        public String profile() {
            return profile;
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
            return abi;
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
        public DexClasses dex() {
            return classes;
        }

        @Override
        public Map<String, Path> uses() {
            return Map.of();
        }

        @Override
        public void addDex(String s) {
            throw new AssertionError();
        }

        @Override
        public void putEntry(Path p, String n) {
            throw new AssertionError();
        }

        @Override
        public void removeEntry(String n) {
            throw new AssertionError();
        }

        @Override
        public void useLibraries(List<String> n) {
            throw new AssertionError();
        }
    }

    static List<String> lines(DexClasses classes) {
        var method = classes.get(TYPE).getMethods().iterator().next();
        var code = method.getImplementation();

        return DexEdits.render(
                code, new DexText.Frame(code.getRegisterCount(), DexEdits.parameters(method)));
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]).toRealPath();
        Path work = Files.createTempDirectory("nxp-phone-connect-anr-").toRealPath();
        Path recipeFile =
                root.resolve(
                        "profiles/"
                                + TestProfiles.declaring(root, "nxp-phone-connect-anr")
                                + "/patches/"
                                + NAME);
        var recipe = Json.object(Json.parse(Files.readString(recipeFile)));

        recipe.put("sourceSha256", FilesSupport.hash(recipeFile));
        check(Recipes.hasRegisteredCoverage(NAME, recipe), "Registered handler coverage required");
        check(
                Json.str(Json.obj(recipe, "constants"), "INPUT_SHA256").equals(PIN),
                "Recipe must pin the OEM Voice APK input");
        Target target = new Target();
        target.work = work;
        var fixture =
                DexEdits.addMethod(
                        new org.jf.dexlib2.immutable.ImmutableClassDef(
                                TYPE, 1, "Ljava/lang/Object;", null, null, null, null, null),
                        "initValueNotifier()V",
                        2,
                        5,
                        ORIGINAL);
        Map<String, byte[]> archive =
                new java.util.TreeMap<>(
                        Map.of(
                                "classes2.dex",
                                FrameworkDexMaterializer.serialize(
                                        new org.jf.dexlib2.immutable.ImmutableDexFile(
                                                org.jf.dexlib2.Opcodes.forApi(28),
                                                List.of(fixture)))));
        target.classes = new DexClasses(archive, List.of("classes2.dex"), 28);
        int expectedRejections = 3;

        for (int mode = 1; mode < 4; mode++) {
            target.profile = TestProfiles.SYNTHETIC;
            target.api = mode == 1 ? 30 : 28;

            if (mode == 2) {
                target.abi = "armeabi-v7a";
            } else {
                target.abi = "arm64-v8a";
            }

            if (mode == 3) {
                target.pin = "0".repeat(64);
            } else {
                target.pin = PIN;
            }

            var mutated = Json.object(Json.parse(Files.readString(recipeFile)));
            mutated.put("sourceSha256", FilesSupport.hash(recipeFile));
            boolean refused = false;

            try {
                Recipes.apply(NAME, mutated, target);
            } catch (Failure expected) {
                refused = true;
            }

            check(refused, "API/ABI/input mutation refused " + mode);

            if (refused) {
                expectedRejections--;
            }

            check(
                    lines(target.classes).equals(ORIGINAL),
                    "Rejected recipe must not modify the class " + mode);
        }

        target.profile = TestProfiles.SYNTHETIC;
        target.api = 28;
        target.abi = "arm64-v8a";
        target.pin = PIN;
        check(expectedRejections == 0, "All three mutation guards must reject before mutation");
        Recipes.apply(NAME, recipe, target);
        check(
                lines(target.classes).equals(PATCHED),
                "Observers must move onto a dedicated HandlerThread looper; only the allocator"
                        + " changed");
        boolean reapplied = false;

        try {
            Recipes.apply(NAME, recipe, target);
        } catch (Failure expected) {
            reapplied = expected.code.equals("DEX_EDIT_ANCHOR");
        }

        check(reapplied, "Reapplication must be rejected by the absent anchor");
        var definition =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve(
                                                "profiles/"
                                                        + TestProfiles.declaring(
                                                                root, "nxp-phone-connect-anr")
                                                        + "/profile.json"))));
        check(
                Json.list(
                                Json.obj(Json.obj(definition, "patches"), "WakeupSpeechService")
                                        .get("steps"))
                        .contains(NAME),
                "Voice APK build must include the ANR relocation step");
        System.out.println(
                "PASS NXP PhoneConnectMgr ANR relocation: OEM observers moved to a dedicated"
                        + " HandlerThread; profile/API/ABI/input/reapply guards before mutation;"
                        + " production step wired");
    }
}
