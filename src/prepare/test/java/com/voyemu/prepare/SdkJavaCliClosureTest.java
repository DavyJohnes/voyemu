package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.ImmutableMethodParameter;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction10x;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Restored standalone SDK commands retain executable DEX outside the boot classpath. */
public final class SdkJavaCliClosureTest {
    private static final List<String> STEMS =
            List.of(
                    "am",
                    "bmgr",
                    "bu",
                    "content",
                    "dpm",
                    "input",
                    "monkey",
                    "requestsync",
                    "sm",
                    "svc",
                    "uiautomator",
                    "android.test.runner");
    private static final List<String> MAINS =
            List.of(
                    "am.Am",
                    "bmgr.Bmgr",
                    "bu.Backup",
                    "content.Content",
                    "dpm.Dpm",
                    "input.Input",
                    "monkey.Monkey",
                    "requestsync.RequestSync",
                    "sm.Sm",
                    "svc.Svc",
                    "uiautomator.Launcher");

    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static void executable(List<byte[]> dex, String descriptor) {
        for (byte[] bytes : dex) {
            for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(28), bytes).getClasses()) {
                if (!type.getType().equals(descriptor)) {
                    continue;
                }

                for (var method : type.getMethods()) {
                    if (method.getName().equals("main")
                            && method.getParameterTypes().equals(List.of("[Ljava/lang/String;"))
                            && method.getReturnType().equals("V")
                            && (method.getAccessFlags() & 9) == 9
                            && method.getImplementation() != null) {
                        return;
                    }
                }
            }
        }

        throw new AssertionError("Missing executable public static main: " + descriptor);
    }

    private static void runner(List<byte[]> dex) {
        var classes = new java.util.HashSet<String>();

        for (byte[] bytes : dex) {
            classes.addAll(NativeCore.classes(new DexBackedDexFile(Opcodes.forApi(28), bytes)));
        }

        check(classes.contains("Landroid/test/InstrumentationTestRunner;"), "runner classpath");
        check(classes.contains("Landroid/test/AndroidTestRunner;"), "test runner dependency");
    }

    private static byte[] fixture(String descriptor, int flags, List<String> parameters)
            throws Exception {
        var method =
                new ImmutableMethod(
                        descriptor,
                        "main",
                        parameters.stream()
                                .map(type -> new ImmutableMethodParameter(type, Set.of(), null))
                                .toList(),
                        "V",
                        flags,
                        Set.of(),
                        Set.of(),
                        new ImmutableMethodImplementation(
                                1,
                                List.of(new ImmutableInstruction10x(Opcode.RETURN_VOID)),
                                List.of(),
                                List.of()));
        var type =
                new ImmutableClassDef(
                        descriptor,
                        1,
                        "Ljava/lang/Object;",
                        List.of(),
                        null,
                        Set.of(),
                        List.of(),
                        List.of(),
                        List.of(method),
                        List.of());

        return FrameworkDexMaterializer.serialize(
                new ImmutableDexFile(Opcodes.forApi(28), List.of(type)));
    }

    private static void rejected(List<byte[]> dex, String descriptor) {
        try {
            executable(dex, descriptor);
        } catch (AssertionError expected) {
            return;
        }

        throw new AssertionError("Invalid CLI closure accepted");
    }

    private static void unit() throws Exception {
        String descriptor = "Lcom/android/commands/content/Content;";

        executable(List.of(fixture(descriptor, 9, List.of("[Ljava/lang/String;"))), descriptor);
        rejected(List.of(), descriptor); // A retained stripped SDK JAR supplies no executable DEX.
        rejected(List.of(fixture(descriptor, 1, List.of("[Ljava/lang/String;"))), descriptor);
        rejected(List.of(fixture(descriptor, 9, List.of())), descriptor);
        rejected(
                List.of(fixture("Lfixture/Other;", 9, List.of("[Ljava/lang/String;"))), descriptor);

        try {
            runner(List.of(fixture(descriptor, 9, List.of("[Ljava/lang/String;"))));
            throw new AssertionError("Missing runner dependency accepted");
        } catch (AssertionError expected) {
            check(expected.getMessage().equals("runner classpath"), "runner negative oracle");
        }
    }

    public static void main(String[] args) throws Exception {
        unit();

        if (args.length == 0) {
            System.out.println(
                    "PASS standalone CLI executable/stripped/method/runner negative guards");

            return;
        }

        check(args.length == 37, "ROOT followed by twelve JAR/OAT/VDEX triples required");
        Path root = Path.of(args[0]);

        for (String profile :
                List.of("free-09-chinese-20260106-6.0.3", "free-09-chinese-20221230-nxp-rc45")) {
            for (int index = 0; index < STEMS.size(); index++) {
                String stem = STEMS.get(index);
                Path jar = Path.of(args[1 + index * 3]);

                List<byte[]> retainedDex =
                        NativeCore.entries(jar).entrySet().stream()
                                .filter(entry -> entry.getKey().matches("classes[0-9]*\\.dex"))
                                .map(Map.Entry::getValue)
                                .toList();
                check(retainedDex.isEmpty(), stem + " clean SDK JAR unexpectedly contains DEX");

                if (index < MAINS.size()) {
                    rejected(
                            retainedDex,
                            "Lcom/android/commands/" + MAINS.get(index).replace('.', '/') + ";");
                }

                Path definition =
                        root.resolve(
                                "profiles/"
                                        + profile
                                        + "/components/base-"
                                        + stem
                                        + "/component.json");
                Map<String, Object> component =
                        Json.object(Json.parse(Files.readString(definition)));
                Map<String, Object> base = Json.obj(component, "frameworkBase");
                check(
                        Json.str(base, "jar").equals("/system/framework/" + stem + ".jar"),
                        "classpath target");
                byte[] oat = Files.readAllBytes(Path.of(args[2 + index * 3]));
                byte[] vdex = Files.readAllBytes(Path.of(args[3 + index * 3]));
                var restored =
                        FrameworkDexMaterializer.materialize(
                                oat,
                                Json.str(base, "oat_sha256"),
                                vdex,
                                Json.str(base, "vdex_sha256"));
                check(!restored.dexFiles().isEmpty(), stem + " materialization is empty");

                if (index < MAINS.size()) {
                    executable(
                            restored.dexFiles(),
                            "Lcom/android/commands/" + MAINS.get(index).replace('.', '/') + ";");
                } else {
                    runner(restored.dexFiles());
                }

                System.out.println("PASS " + profile + ":" + stem);
            }
        }
    }
}
