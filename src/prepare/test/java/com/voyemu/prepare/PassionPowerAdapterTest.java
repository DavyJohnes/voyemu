package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Map;
import java.util.TreeMap;

public final class PassionPowerAdapterTest {
    static void check(boolean ok, String text) {
        if (!ok) {
            throw new AssertionError(text);
        }
    }

    interface Run {
        void run() throws Exception;
    }

    static void reject(Run run) throws Exception {
        try {
            run.run();
            throw new AssertionError("unverified power boundary accepted");
        } catch (Failure expected) {
            // Rejection is the expected outcome.
        }
    }

    static Map<String, ClassDef> classes(byte[] dex) {
        var result = new TreeMap<String, ClassDef>();

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            check(result.put(type.getType(), type) == null, "duplicate class");
        }

        return result;
    }

    static boolean target(Method m) {
        return m.getName().equals("updateAllBackLightState")
                || m.getName().equals("updateBackLightState");
    }

    static ClassDef withoutTargets(ClassDef type) {
        var methods = new ArrayList<Method>();

        for (Method method : type.getDirectMethods()) {
            if (!target(method)) {
                methods.add(method);
            }
        }

        return new ImmutableClassDef(
                type.getType(),
                type.getAccessFlags(),
                type.getSuperclass(),
                type.getInterfaces(),
                type.getSourceFile(),
                type.getAnnotations(),
                type.getStaticFields(),
                type.getInstanceFields(),
                methods,
                type.getVirtualMethods());
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 3) {
            throw new IllegalArgumentException("ROOT OWNER_FRAMEWORK ANDROID_JAR");
        }

        Path root = Path.of(args[0]).toRealPath(),
                original = Path.of(args[1]).toRealPath(),
                platform = Path.of(args[2]).toRealPath();
        var definition =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve(
                                                "profiles/"
                                                        + PassionPowerAdapter.PROFILE
                                                        + "/components/mega-compat/component.json"))));
        var recipe = Json.obj(definition, "donorDex");
        Path work =
                Files.createTempDirectory(root.resolve(".local/build/tmp"), "passion-power-test-");

        try {
            Path baseline =
                    DexOperations.donorJar(
                            original,
                            recipe,
                            work.resolve("absent"),
                            platform,
                            work.resolve("before"),
                            "before.jar",
                            30);
            byte[] before = NativeCore.entries(baseline).get("classes.dex");
            var source = classes(before);
            Path patched =
                    DexOperations.donorJar(
                            original,
                            recipe,
                            work.resolve("absent"),
                            platform,
                            work.resolve("after"),
                            "voyemu-mega.jar",
                            new DexOperations.DonorTarget(
                                    30, PassionPowerAdapter.PROFILE, PassionPowerAdapter.NAME));
            byte[] bytes = NativeCore.entries(patched).get("classes.dex");
            var after = classes(bytes);
            check(
                    source.size() == 26 && source.keySet().equals(after.keySet()),
                    "exact26 class inventory preserved");
            int replaced = 0;

            for (var item : source.entrySet()) {
                if (!item.getKey().equals(PassionPowerAdapter.OWNER)) {
                    check(
                            FrameworkDexMaterializer.semantic(
                                            PassionStandaloneLibrary.withoutBootMetadata(
                                                    item.getValue()))
                                    .equals(
                                            FrameworkDexMaterializer.semantic(
                                                    after.get(item.getKey()))),
                            "all25 other class bodies/metadata unchanged");
                } else {
                    check(
                            FrameworkDexMaterializer.semantic(
                                            PassionStandaloneLibrary.withoutBootMetadata(
                                                    withoutTargets(item.getValue())))
                                    .equals(
                                            FrameworkDexMaterializer.semantic(
                                                    withoutTargets(after.get(item.getKey())))),
                            "constructor/non-target methods/fields/class metadata unchanged");

                    for (Method method : after.get(item.getKey()).getDirectMethods()) {
                        if (target(method)) {
                            var instructions = new ArrayList<Instruction>();
                            method.getImplementation().getInstructions().forEach(instructions::add);
                            check(
                                    instructions.size() == 2
                                            && instructions.get(0).getOpcode()
                                                    == org.jf.dexlib2.Opcode.CONST_4
                                            && ((NarrowLiteralInstruction) instructions.get(0))
                                                            .getNarrowLiteral()
                                                    == 0
                                            && ((OneRegisterInstruction) instructions.get(0))
                                                            .getRegisterA()
                                                    == 0
                                            && instructions.get(1).getOpcode()
                                                    == org.jf.dexlib2.Opcode.RETURN
                                            && ((OneRegisterInstruction) instructions.get(1))
                                                            .getRegisterA()
                                                    == 0,
                                    "only unsupported false body without hardware call");
                            ++replaced;
                        }
                    }
                }
            }

            check(replaced == 2, "exactly2 unsupported methods");
            reject(() ->
                    PassionPowerAdapter.apply(
                            "free-11-chinese-20260731-7.3.1", 30, original, before));
            reject(() ->
                    PassionPowerAdapter.apply(PassionPowerAdapter.PROFILE, 28, original, before));
            reject(() ->
                    PassionPowerAdapter.apply(PassionPowerAdapter.PROFILE, 30, original, bytes));
            Path wrong = work.resolve("wrong.jar");
            NativeCore.zip(wrong, Map.of("classes.dex", before), false);
            reject(() -> PassionPowerAdapter.apply(PassionPowerAdapter.PROFILE, 30, wrong, before));
            Path invalid = work.resolve("invalid");
            reject(() ->
                    DexOperations.donorJar(
                            original,
                            recipe,
                            work.resolve("absent"),
                            platform,
                            invalid,
                            "x.jar",
                            new DexOperations.DonorTarget(
                                    30, PassionPowerAdapter.PROFILE, "unknown")));
            check(!Files.exists(invalid), "unknown adapter rejected before output");
            check(
                    FilesSupport.hash(original).equals(PassionPowerAdapter.PIN),
                    "owner original unchanged");
        } finally {
            FilesSupport.removeOwnedTree(work);
        }

        System.out.println(
                "Actual Passion26classes: exact2 unsupportedfalse bodies; other code/access"
                        + " semantics (only hidden boot flags normalized)/pins/profile/API/reapply"
                        + " guards passed; guest=false");
    }
}
