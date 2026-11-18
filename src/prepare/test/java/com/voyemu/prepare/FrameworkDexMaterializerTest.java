package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction10x;

import java.util.List;
import java.util.Set;

public final class FrameworkDexMaterializerTest {
    public static void main(String[] args) throws Exception {
        // Private visibility2 encoded with hidden first bit =>5. Direct membership must survive.
        var method =
                new ImmutableMethod(
                        "Ltest/Hidden;",
                        "f",
                        List.of(),
                        "V",
                        5,
                        Set.of(),
                        Set.of(),
                        new ImmutableMethodImplementation(
                                1,
                                List.of(new ImmutableInstruction10x(Opcode.RETURN_VOID)),
                                List.of(),
                                List.of()));
        var type =
                new ImmutableClassDef(
                        "Ltest/Hidden;",
                        1,
                        "Ljava/lang/Object;",
                        List.of(),
                        null,
                        Set.of(),
                        List.of(),
                        List.of(),
                        List.of(method),
                        List.of());
        var dex = new ImmutableDexFile(Opcodes.forApi(28), List.of(type));
        byte[] serialized = FrameworkDexMaterializer.serialize(dex);
        var reread = new DexBackedDexFile(Opcodes.forApi(28), serialized);
        var result = reread.getClasses().iterator().next();

        VdexQuickeningTest.check(result.getDirectMethods().iterator().next().getAccessFlags() == 5);
        VdexQuickeningTest.check(!result.getVirtualMethods().iterator().hasNext());
        VdexQuickeningTest.check(
                FrameworkDexMaterializer.semantic(dex.getClasses())
                        .equals(FrameworkDexMaterializer.semantic(reread.getClasses())));
        var changed =
                new ImmutableClassDef(
                        "Ltest/Hidden;",
                        1,
                        "Ljava/lang/Object;",
                        List.of(),
                        null,
                        Set.of(),
                        List.of(),
                        List.of(),
                        List.of(),
                        List.of(method));
        VdexQuickeningTest.check(
                !FrameworkDexMaterializer.semantic(type)
                        .equals(FrameworkDexMaterializer.semantic(changed)));
        var reference =
                new org.jf.dexlib2.immutable.reference.ImmutableMethodReference(
                        "Ljava/lang/invoke/MethodHandle;",
                        "invokeExact",
                        List.of("[Ljava/lang/Object;"),
                        "Ljava/lang/Object;");
        var proto =
                new org.jf.dexlib2.immutable.reference.ImmutableMethodProtoReference(
                        List.of("Ljava/lang/String;"), "Ljava/lang/String;");
        var polymorphic =
                new ImmutableMethod(
                        "Ltest/Poly;",
                        "f",
                        List.of(),
                        "V",
                        9,
                        Set.of(),
                        Set.of(),
                        new ImmutableMethodImplementation(
                                2,
                                List.of(
                                        new org.jf.dexlib2.immutable.instruction
                                                .ImmutableInstruction45cc(
                                                Opcode.INVOKE_POLYMORPHIC,
                                                2,
                                                0,
                                                1,
                                                0,
                                                0,
                                                0,
                                                reference,
                                                proto),
                                        new ImmutableInstruction10x(Opcode.RETURN_VOID)),
                                List.of(),
                                List.of()));
        var polyType =
                new ImmutableClassDef(
                        "Ltest/Poly;",
                        1,
                        "Ljava/lang/Object;",
                        List.of(),
                        null,
                        Set.of(),
                        List.of(),
                        List.of(),
                        List.of(polymorphic),
                        List.of());
        byte[] poly =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(28), List.of(polyType)));
        var polyRead = new DexBackedDexFile(Opcodes.forApi(28), poly);
        VdexQuickeningTest.check(
                FrameworkDexMaterializer.semantic(polyType)
                        .equals(
                                FrameworkDexMaterializer.semantic(
                                        polyRead.getClasses().iterator().next())));
        VdexQuickeningTest.check(
                !FrameworkDexMaterializer.defaultValue(
                        new org.jf.dexlib2.immutable.value.ImmutableFloatEncodedValue(-0.0f)));
        VdexQuickeningTest.check(
                FrameworkDexMaterializer.defaultValue(
                        new org.jf.dexlib2.immutable.value.ImmutableFloatEncodedValue(0.0f)));
        VdexQuickeningTest.check(
                !FrameworkDexMaterializer.semantic("\ud800")
                        .equals(FrameworkDexMaterializer.semantic("\ud801")));
        VdexQuickeningTest.check(
                !FrameworkDexMaterializer.semantic(Float.intBitsToFloat(0x7fc00001))
                        .equals(
                                FrameworkDexMaterializer.semantic(
                                        Float.intBitsToFloat(0x7fc00002))));
        System.out.println(
                "Raw hiddenAPI flags, direct/virtual partition, polymorphic reference2,"
                        + " UTF16/raw-float and semantic mutation tests passed");
    }
}
