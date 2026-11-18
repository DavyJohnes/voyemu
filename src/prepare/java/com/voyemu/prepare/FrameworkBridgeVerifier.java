package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.immutable.ImmutableClassDef;

import java.util.ArrayList;
import java.util.Map;
import java.util.TreeMap;

/** Acceptance gate: only the registered ViewDebug wrapper and helper definitions may change. */
final class FrameworkBridgeVerifier {
    private FrameworkBridgeVerifier() {}

    private static Map<String, ClassDef> classes(byte[] bytes) {
        Map<String, ClassDef> result = new TreeMap<>();

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(28), bytes).getClasses()) {
            require(result.put(type.getType(), ImmutableClassDef.of(type)) == null);
        }

        return result;
    }

    private static void require(boolean value) {
        Failure.require(
                value,
                "FRAMEWORK_BRIDGE_SEMANTICS",
                "Observation bridge changed unrelated framework metadata",
                5);
    }

    // A framework pool can exceed 65535 strings. Use one stable instruction width
    // before merging, with dexlib labels maintaining branch/debug/try addresses.
    static byte[] jumboHelper(byte[] helper) throws Exception {
        var types = new ArrayList<ClassDef>();

        for (ClassDef type : classes(helper).values()) {
            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();

            for (Method method : type.getDirectMethods()) {
                direct.add(jumboMethod(method));
            }

            for (Method method : type.getVirtualMethods()) {
                virtual.add(jumboMethod(method));
            }

            types.add(
                    new ImmutableClassDef(
                            type.getType(),
                            type.getAccessFlags(),
                            type.getSuperclass(),
                            type.getInterfaces(),
                            type.getSourceFile(),
                            type.getAnnotations(),
                            type.getStaticFields(),
                            type.getInstanceFields(),
                            direct,
                            virtual));
        }

        return FrameworkDexMaterializer.serialize(
                new org.jf.dexlib2.immutable.ImmutableDexFile(Opcodes.forApi(28), types));
    }

    private static Method jumboMethod(Method method) {
        if (method.getImplementation() == null) {
            return method;
        }

        var implementation =
                new org.jf.dexlib2.builder.MutableMethodImplementation(method.getImplementation());

        for (int index = 0; index < implementation.getInstructions().size(); index++) {
            var instruction = implementation.getInstructions().get(index);

            if (instruction.getOpcode() == org.jf.dexlib2.Opcode.CONST_STRING) {
                var source = (org.jf.dexlib2.iface.instruction.formats.Instruction21c) instruction;
                implementation.replaceInstruction(
                        index,
                        new org.jf.dexlib2.builder.instruction.BuilderInstruction31c(
                                org.jf.dexlib2.Opcode.CONST_STRING_JUMBO,
                                source.getRegisterA(),
                                source.getReference()));
            }
        }

        return new org.jf.dexlib2.immutable.ImmutableMethod(
                method.getDefiningClass(),
                method.getName(),
                method.getParameters(),
                method.getReturnType(),
                method.getAccessFlags(),
                method.getAnnotations(),
                method.getHiddenApiRestrictions(),
                implementation);
    }

    static void verify(byte[] source, byte[] helper, byte[] patched) throws Exception {
        Map<String, ClassDef> original = classes(source),
                donor = classes(helper),
                actual = classes(patched);

        require(actual.size() == original.size() + donor.size());

        for (var entry : donor.entrySet()) {
            require(!original.containsKey(entry.getKey()) && actual.containsKey(entry.getKey()));
            Failure.require(
                    FrameworkDexMaterializer.semantic(entry.getValue())
                            .equals(FrameworkDexMaterializer.semantic(actual.get(entry.getKey()))),
                    "FRAMEWORK_BRIDGE_SEMANTICS",
                    "Helper definition changed: "
                            + entry.getKey()
                            + " "
                            + FrameworkDexMaterializer.difference(
                                    entry.getValue(), actual.get(entry.getKey())),
                    5);
        }

        for (var entry : original.entrySet()) {
            ClassDef after = actual.get(entry.getKey());
            require(after != null);

            if (entry.getKey().equals(ViewDumpPatch.OWNER)) {
                Method target = null;

                for (Method method : entry.getValue().getDirectMethods()) {
                    if (ViewDumpPatch.target(
                            method, ViewDumpPatch.OWNER, ViewDumpPatch.PARAMETERS)) {
                        require(target == null);
                        target = method;
                    }
                }

                require(target != null);
                var methods = new ArrayList<Method>();
                int replaced = 0;

                for (Method method : after.getDirectMethods()) {
                    if (ViewDumpPatch.target(
                            method, ViewDumpPatch.OWNER, ViewDumpPatch.PARAMETERS)) {
                        methods.add(
                                new org.jf.dexlib2.immutable.ImmutableMethod(
                                        method.getDefiningClass(),
                                        method.getName(),
                                        method.getParameters(),
                                        method.getReturnType(),
                                        method.getAccessFlags(),
                                        method.getAnnotations(),
                                        method.getHiddenApiRestrictions(),
                                        target.getImplementation()));
                        replaced++;
                    } else {
                        methods.add(method);
                    }
                }

                require(replaced == 1);
                after =
                        new ImmutableClassDef(
                                after.getType(),
                                after.getAccessFlags(),
                                after.getSuperclass(),
                                after.getInterfaces(),
                                after.getSourceFile(),
                                after.getAnnotations(),
                                after.getStaticFields(),
                                after.getInstanceFields(),
                                methods,
                                after.getVirtualMethods());
            }

            Failure.require(
                    FrameworkDexMaterializer.semantic(entry.getValue())
                            .equals(FrameworkDexMaterializer.semantic(after)),
                    "FRAMEWORK_BRIDGE_SEMANTICS",
                    "Framework definition changed: " + entry.getKey(),
                    5);
        }
    }
}
