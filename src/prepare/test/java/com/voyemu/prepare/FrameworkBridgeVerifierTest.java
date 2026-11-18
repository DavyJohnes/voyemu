package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction10x;

import java.util.List;

public final class FrameworkBridgeVerifierTest {
    public static void main(String[] args) throws Exception {
        var branchMethod =
                ViewDumpPatchTest.method(
                        ViewDumpPatch.HELPER,
                        List.of(),
                        9,
                        1,
                        List.of(
                                new org.jf.dexlib2.immutable.instruction.ImmutableInstruction21t(
                                        Opcode.IF_EQZ, 0, 4),
                                new org.jf.dexlib2.immutable.instruction.ImmutableInstruction21c(
                                        Opcode.CONST_STRING,
                                        0,
                                        new org.jf.dexlib2.immutable.reference
                                                .ImmutableStringReference("value")),
                                new ImmutableInstruction10x(Opcode.RETURN_VOID)));
        byte[] narrow =
                ViewDumpPatchTest.dex(
                        List.of(
                                ViewDumpPatchTest.type(
                                        ViewDumpPatch.HELPER, List.of(branchMethod))));
        byte[] jumbo = FrameworkBridgeVerifier.jumboHelper(narrow);
        var normalized =
                new org.jf.dexlib2.dexbacked.DexBackedDexFile(
                        org.jf.dexlib2.Opcodes.forApi(28), jumbo);
        var instructions =
                normalized
                        .getClasses()
                        .iterator()
                        .next()
                        .getMethods()
                        .iterator()
                        .next()
                        .getImplementation()
                        .getInstructions()
                        .iterator();

        if (((org.jf.dexlib2.iface.instruction.OffsetInstruction) instructions.next())
                                .getCodeOffset()
                        != 5
                || instructions.next().getOpcode() != Opcode.CONST_STRING_JUMBO
                || !java.util.Arrays.equals(jumbo, FrameworkBridgeVerifier.jumboHelper(jumbo))) {
            throw new AssertionError("jumbo branch labels");
        }

        byte[] source = ViewDumpPatchTest.source(42, 4, "");

        byte[] helper = ViewDumpPatchTest.helper(ViewDumpPatch.HELPER);
        byte[] patched =
                ViewDumpPatch.patch(
                        source, ViewDumpPatch.sha(source), helper, ViewDumpPatch.sha(helper));
        FrameworkBridgeVerifier.verify(source, helper, patched);
        var dex =
                new org.jf.dexlib2.dexbacked.DexBackedDexFile(
                        org.jf.dexlib2.Opcodes.forApi(28), patched);
        var changed = new java.util.ArrayList<org.jf.dexlib2.iface.ClassDef>();

        for (var type : dex.getClasses()) {
            if (type.getType().equals("Lfixture/Unchanged;")) {
                changed.add(
                        ViewDumpPatchTest.type(
                                type.getType(),
                                List.of(
                                        ViewDumpPatchTest.method(
                                                type.getType(),
                                                List.of(),
                                                9,
                                                1,
                                                List.of(
                                                        new ImmutableInstruction10x(
                                                                Opcode.RETURN_VOID))))));
            } else {
                changed.add(type);
            }
        }

        try {
            FrameworkBridgeVerifier.verify(source, helper, ViewDumpPatchTest.dex(changed));
            throw new AssertionError("unrelated mutation accepted");
        } catch (Failure expected) {
            if (!expected.code.equals("FRAMEWORK_BRIDGE_SEMANTICS")) {
                throw expected;
            }
        }

        System.out.println("PASS bridge exact semantic scope and unrelated mutation rejection");
    }
}
