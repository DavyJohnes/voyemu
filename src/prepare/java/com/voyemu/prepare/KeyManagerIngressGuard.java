package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction35c;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeMap;
import java.util.zip.ZipFile;

/** Inactive exact tx7 ingress adapter; all existing OEM reader and timer code stays intact. */
final class KeyManagerIngressGuard {
    static final String NAME = "api30-key-ingress",
            OWNER = "Lcom/qinggan/keymanager/service/KeyManagerService$1;";
    private static final String ENGINE = "Lcom/qinggan/keymanager/service/engine/KeyManagerEngine;",
            MESSAGE = "Lcom/qinggan/keymanager/service/engine/KeyManagerMessage;";
    private static final ImmutableMethodReference BEFORE =
            new ImmutableMethodReference(ENGINE, "pushMsg", List.of(MESSAGE), "Z");
    private static final ImmutableMethodReference AFTER =
            new ImmutableMethodReference(
                    "Lcom/voyemu/input/FreeWheelIngress;",
                    "route",
                    List.of("Ljava/lang/Object;", "Ljava/lang/Object;"),
                    "Z");

    private static void require(boolean value) {
        Failure.require(value, "KEY_INGRESS_CONTRACT", "Exact Free tx7 Binder ingress required", 3);
    }

    private static boolean target(Method method) {
        return method.getDefiningClass().equals(OWNER)
                && method.getName().equals("inputQGKeyEvent")
                && method.getParameterTypes()
                        .equals(List.of("Lcom/qinggan/system/QGKeyEvent;", "Ljava/lang/String;"))
                && method.getReturnType().equals("V");
    }

    static void validateMethod(Method method, boolean inverse) {
        require(
                target(method)
                        && method.getAccessFlags() == 1
                        && method.getImplementation() != null);
        var body = method.getImplementation();
        require(body.getRegisterCount() == 5 && body.getTryBlocks().isEmpty());
        int address = 0, found = 0;

        for (Instruction instruction : body.getInstructions()) {
            if (address == 45) {
                require(
                        instruction instanceof FiveRegisterInstruction
                                && instruction instanceof ReferenceInstruction referenceInstruction
                                && instruction.getOpcode()
                                        == (inverse ? Opcode.INVOKE_STATIC : Opcode.INVOKE_VIRTUAL)
                                && referenceInstruction
                                        .getReference()
                                        .equals(inverse ? AFTER : BEFORE));
                var registers = (FiveRegisterInstruction) instruction;
                require(
                        registers.getRegisterCount() == 2
                                && registers.getRegisterC() == 3
                                && registers.getRegisterD() == 0
                                && registers.getRegisterE() == 0
                                && registers.getRegisterF() == 0
                                && registers.getRegisterG() == 0);
                found++;
            }

            address += instruction.getCodeUnits();
        }

        require(address == 49 && found == 1);
    }

    private static Method rewrite(Method method, boolean inverse, int[] count) {
        if (!target(method)) {
            return method;
        }

        validateMethod(method, inverse);
        var body = method.getImplementation();
        var output = new ArrayList<Instruction>();
        int address = 0;

        for (Instruction instruction : body.getInstructions()) {
            if (address == 45) {
                output.add(
                        new ImmutableInstruction35c(
                                inverse ? Opcode.INVOKE_VIRTUAL : Opcode.INVOKE_STATIC,
                                2,
                                3,
                                0,
                                0,
                                0,
                                0,
                                inverse ? BEFORE : AFTER));
                count[0]++;
            } else {
                output.add(instruction);
            }

            address += instruction.getCodeUnits();
        }

        return new ImmutableMethod(
                method.getDefiningClass(),
                method.getName(),
                method.getParameters(),
                method.getReturnType(),
                method.getAccessFlags(),
                method.getAnnotations(),
                method.getHiddenApiRestrictions(),
                new ImmutableMethodImplementation(
                        body.getRegisterCount(),
                        output,
                        body.getTryBlocks(),
                        body.getDebugItems()));
    }

    private static ImmutableDexFile rewrite(DexFile source, boolean inverse) {
        var output = new TreeMap<String, ClassDef>();
        int[] count = {0};

        for (ClassDef type : source.getClasses()) {
            if (!type.getType().equals(OWNER)) {
                require(output.put(type.getType(), type) == null);
                continue;
            }

            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();

            for (Method method : type.getDirectMethods()) {
                direct.add(rewrite(method, inverse, count));
            }

            for (Method method : type.getVirtualMethods()) {
                virtual.add(rewrite(method, inverse, count));
            }

            require(
                    output.put(
                                    type.getType(),
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
                                            virtual))
                            == null);
        }

        require(count[0] == 1);

        return new ImmutableDexFile(Opcodes.forApi(30), output.values());
    }

    static byte[] validateOriginal(String profile, int api, Path original) throws Exception {
        require(
                KeyManagerGuard.VARIANTS.containsKey(profile)
                        && api == 30
                        && original != null
                        && FilesSupport.hash(original)
                                .equals(KeyManagerGuard.VARIANTS.get(profile).pin()));
        byte[] source;

        try (var zip = new ZipFile(original.toFile())) {
            var entry = zip.getEntry("classes.dex");
            require(entry != null && entry.getSize() > 0 && entry.getSize() <= 32 * 1024 * 1024);
            source = zip.getInputStream(entry).readAllBytes();
            require(source.length == entry.getSize());
        }

        rewrite(new DexBackedDexFile(Opcodes.forApi(30), source), false);

        return source;
    }

    static byte[] apply(String profile, int api, Path original, byte[] current) throws Exception {
        validateOriginal(profile, api, original);
        require(current != null);
        var before = new DexBackedDexFile(Opcodes.forApi(30), current);
        byte[] output = FrameworkDexMaterializer.serialize(rewrite(before, false));
        var normalized = rewrite(new DexBackedDexFile(Opcodes.forApi(30), output), true);
        var names = new TreeMap<String, ClassDef>();

        for (ClassDef type : normalized.getClasses()) {
            require(names.put(type.getType(), type) == null);
        }

        require(names.size() == before.getClasses().size());

        for (ClassDef type : before.getClasses()) {
            require(
                    names.containsKey(type.getType())
                            && FrameworkDexMaterializer.semantic(type)
                                    .equals(
                                            FrameworkDexMaterializer.semantic(
                                                    names.get(type.getType()))));
        }

        require(FilesSupport.hash(original).equals(KeyManagerGuard.VARIANTS.get(profile).pin()));

        return output;
    }
}
