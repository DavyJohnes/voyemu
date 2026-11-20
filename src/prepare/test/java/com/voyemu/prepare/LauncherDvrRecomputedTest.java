package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10x;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

public final class LauncherDvrRecomputedTest {
    interface Check {
        void run() throws Exception;
    }

    static void rejects(Check c) throws Exception {
        try {
            c.run();
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError("DVR guard accepted drift");
    }

    static int calls(byte[] dex, String owner, String name, String ref) {
        var m = LauncherDvrGuard.Recomputed.method(LauncherDvrGuard.classes(dex), owner, name);
        int count = 0;

        for (var i : m.getImplementation().getInstructions()) {
            if (i instanceof ReferenceInstruction r && r.getReference().toString().equals(ref)) {
                count++;
            }
        }

        return count;
    }

    static byte[] mutateUnrelated(byte[] dex) throws Exception {
        var cs = LauncherDvrGuard.classes(dex);
        var owner = cs.get(LauncherPowerGuard.POWER_OWNER);

        if (owner == null) {
            throw new AssertionError("power owner missing");
        }

        var direct = new ArrayList<Method>();
        var virtual = new ArrayList<Method>();
        int changed = 0;
        int groupIndex = 0;

        for (var group : List.of(owner.getDirectMethods(), owner.getVirtualMethods())) {
            var dest = groupIndex++ == 0 ? direct : virtual;

            for (var m : group) {
                if (m.getName().equals("<init>") && m.getParameterTypes().isEmpty()) {
                    var body = new MutableMethodImplementation(m.getImplementation());
                    body.addInstruction(0, new BuilderInstruction10x(Opcode.NOP));
                    dest.add(PassionServiceForegroundGuard.copy(m, body));
                    changed++;
                } else {
                    dest.add(m);
                }
            }
        }

        if (changed != 1) {
            throw new AssertionError("unrelated mutation target missing");
        }

        cs.put(
                owner.getType(),
                new ImmutableClassDef(
                        owner.getType(),
                        owner.getAccessFlags(),
                        owner.getSuperclass(),
                        owner.getInterfaces(),
                        owner.getSourceFile(),
                        owner.getAnnotations(),
                        owner.getStaticFields(),
                        owner.getInstanceFields(),
                        direct,
                        virtual));

        return FrameworkDexMaterializer.serialize(
                new ImmutableDexFile(Opcodes.forApi(30), cs.values()));
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]);
        Map<String, Object>
                constants =
                        TestSteps.select(
                                root,
                                "launcher_dvr_pinned",
                                false,
                                LauncherDvrGuard.NAME,
                                LauncherPowerGuard.NAME,
                                AqyProviderGuard.NAME),
                other =
                        TestSteps.select(
                                root,
                                "launcher_dvr_pinned",
                                true,
                                LauncherDvrGuard.NAME,
                                LauncherPowerGuard.NAME,
                                AqyProviderGuard.NAME);
        Path apk = TestSteps.original(root, constants, "Launcher.apk"),
                foreign = TestSteps.original(root, other, "Launcher.apk");
        String abi = "arm64-v8a";
        byte[] raw = NativeCore.entries(apk).get("classes2.dex");

        LauncherDvrGuard.validateOriginalChain(constants, 30, abi, apk, raw);
        byte[] power = LauncherPowerGuard.apply(constants, 30, apk, raw, "classes2.dex");
        byte[] predecessor = AqyProviderGuard.apply(constants, 30, apk, power);
        byte[] fixed = LauncherDvrGuard.apply(constants, 30, abi, apk, predecessor);
        String select = LauncherDvrGuard.HELPER + "->select(I)I",
                route = LauncherDvrGuard.HELPER + "->useService(Ljava/lang/Object;)Z";

        if (calls(predecessor, LauncherDvrGuard.OWNER, "generateApp", select) != 0
                || calls(fixed, LauncherDvrGuard.OWNER, "generateApp", select) != 2
                || calls(fixed, LauncherDvrGuard.OWNER, "generateHomeCardApp", select) != 1
                || calls(fixed, LauncherDvrGuard.ROUTE, "startAppBySubType", route) != 1) {
            throw new AssertionError("DVR config and route calls differ");
        }

        byte[] unrelated = mutateUnrelated(predecessor);
        ViewDumpPatch.checkedDex(unrelated, NativeCore.hash(unrelated));
        var before = LauncherDvrGuard.classes(predecessor);
        var after = LauncherDvrGuard.classes(unrelated);

        if (FrameworkDexMaterializer.semantic(before.get(LauncherPowerGuard.POWER_OWNER))
                .equals(
                        FrameworkDexMaterializer.semantic(
                                after.get(LauncherPowerGuard.POWER_OWNER)))) {
            throw new AssertionError("unrelated method was not changed");
        }

        for (String name : List.of("generateApp", "generateHomeCardApp")) {
            if (!FrameworkDexMaterializer.semantic(
                            LauncherDvrGuard.Recomputed.method(
                                    before, LauncherDvrGuard.OWNER, name))
                    .equals(
                            FrameworkDexMaterializer.semantic(
                                    LauncherDvrGuard.Recomputed.method(
                                            after, LauncherDvrGuard.OWNER, name)))) {
                throw new AssertionError("fixture altered target");
            }
        }

        if (!FrameworkDexMaterializer.semantic(
                        LauncherDvrGuard.Recomputed.method(
                                before, LauncherDvrGuard.ROUTE, "startAppBySubType"))
                .equals(
                        FrameworkDexMaterializer.semantic(
                                LauncherDvrGuard.Recomputed.method(
                                        after, LauncherDvrGuard.ROUTE, "startAppBySubType")))) {
            throw new AssertionError("fixture altered route");
        }

        rejects(() -> LauncherDvrGuard.apply(constants, 30, abi, foreign, predecessor));
        rejects(() -> LauncherDvrGuard.apply(other, 30, abi, apk, predecessor));
        rejects(() -> LauncherDvrGuard.apply(constants, 30, abi, apk, fixed));
    }
}
