package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Path;
import java.util.ArrayList;

/** Exercise the actual OEM DEX; guest navigation remains a separate check. */
public final class PassionCarSettingsRecyclerGuardTest {
    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    interface Action {
        void run() throws Exception;
    }

    static void reject(Action action) throws Exception {
        boolean denied = false;

        try {
            action.run();
        } catch (Failure expected) {
            denied = true;
        }

        check(denied, "Expected rejection");
    }

    public static void main(String[] args) throws Exception {
        Path apk = Path.of(args[0]);
        byte[] input = NativeCore.entries(apk).get("classes4.dex");
        var source = new DexBackedDexFile(Opcodes.forApi(30), input);
        var before = PassionCarSettingsRecyclerGuard.method(source);

        check(
                before.getImplementation().getInstructions().iterator().next().getOpcode()
                        != Opcode.SGET,
                "RED: OEM does not reuse Car UI recycler without automotive feature");
        byte[] output =
                PassionCarSettingsRecyclerGuard.apply(
                        PassionCarSettingsRecyclerGuard.PROFILE, 30, apk, input);
        var actual = new DexBackedDexFile(Opcodes.forApi(30), output);
        var after = PassionCarSettingsRecyclerGuard.method(actual);
        var ops = new ArrayList<Instruction>();
        after.getImplementation().getInstructions().forEach(ops::add);
        check(
                ops.get(0).getOpcode() == Opcode.SGET
                        && ops.get(1).getOpcode() == Opcode.INVOKE_VIRTUAL
                        && ops.get(2).getOpcode() == Opcode.MOVE_RESULT_OBJECT
                        && ops.get(3).getOpcode() == Opcode.INSTANCE_OF
                        && ops.get(4).getOpcode() == Opcode.IF_EQZ
                        && ops.get(5).getOpcode() == Opcode.CHECK_CAST
                        && ops.get(6).getOpcode() == Opcode.RETURN_OBJECT,
                "Car UI guarded return inserted");
        check(
                ((ReferenceInstruction) ops.get(3))
                        .getReference()
                        .toString()
                        .equals(PassionCarSettingsRecyclerGuard.CAR),
                "Only actual CarUiRecyclerView is reused");
        int offset = ((OffsetInstruction) ops.get(4)).getCodeOffset();
        int size =
                ops.get(4).getCodeUnits() + ops.get(5).getCodeUnits() + ops.get(6).getCodeUnits();
        check(offset == size, "Null and unrelated views branch to unchanged OEM fallback");
        check(
                FrameworkDexMaterializer.semantic(source.getClasses())
                        .equals(
                                FrameworkDexMaterializer.semantic(
                                        PassionCarSettingsRecyclerGuard.rewrite(actual, true)
                                                .getClasses())),
                "Inverse restores all classes and debug metadata");
        reject(() -> PassionCarSettingsRecyclerGuard.apply("other", 30, apk, input));
        reject(() ->
                PassionCarSettingsRecyclerGuard.apply(
                        PassionCarSettingsRecyclerGuard.PROFILE, 29, apk, input));
        System.out.println(
                "OEM recycler RED -> guarded path GREEN; fallback and inverse verified; runtime"
                        + " pending");
    }
}
