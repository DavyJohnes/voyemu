package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.dexbacked.instruction.DexBackedInstruction;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.zip.Adler32;

/**
 * Explicit A9 adaptation: the selected automatic full-window branch enters the existing OEM
 * renderer.
 */
final class Api28PhoneCallWindowGuard {
    static final String PROFILE = "free-09-chinese-20260106-6.0.3";
    static final String APK_PIN =
            "d5a7417be79e2e4bfd70e643df85ae76e1a8a15332a345f126ccc5781f5ba8a3";
    static final String OWNER =
            "Lcom/qinggan/bluetoothphone/logic/manager/DialStatusChangeManager;";
    static final String METHOD =
            OWNER
                    + "->updateCallState(Lcom/qinggan/bluetoothphone/bean/HfpEvents$CallStateEvent;)V";
    static final String OLD = OWNER + "->closeMinOpenMaxWindow(Z)V";
    static final String NEW = OWNER + "->openMaxWindow(Z)V";
    private static final int ADDRESS = 0xa8;

    private static void need(boolean value) {
        Failure.require(
                value,
                "QINGGAN09_PHONE_CALL_WINDOW_CONTRACT",
                "Exact pinned A9 automatic Phone window call required",
                3);
    }

    private static Method target(DexBackedDexFile dex) {
        Method found = null;

        for (var type : dex.getClasses()) {
            for (var method : type.getMethods()) {
                if (method.toString().equals(METHOD)) {
                    need(found == null);
                    found = method;
                }
            }
        }

        need(found != null && found.getAccessFlags() == 2 && found.getImplementation() != null);
        need(
                found.getImplementation().getRegisterCount() == 8
                        && found.getImplementation().getTryBlocks().isEmpty());

        return found;
    }

    private static int methodIndex(DexBackedDexFile dex, String to) {
        int reference = -1;

        for (int index = 0; index < dex.getMethodSection().size(); index++) {
            if (dex.getMethodSection().get(index).toString().equals(to)) {
                need(reference < 0);
                reference = index;
            }
        }

        need(reference >= 0 && reference <= 65535);

        return reference;
    }

    private static byte[] rewrite(byte[] source, boolean inverse) throws Exception {
        var dex = new DexBackedDexFile(Opcodes.forApi(28), source);
        String from = inverse ? NEW : OLD, to = inverse ? OLD : NEW;
        int reference = methodIndex(dex, to);
        byte[] output = source.clone();
        int address = 0, calls = 0, falseDefinitions = 0;

        for (var instruction : target(dex).getImplementation().getInstructions()) {
            if (address <= ADDRESS
                    && instruction.getOpcode().setsRegister()
                    && instruction instanceof OneRegisterInstruction register
                    && register.getRegisterA() == 4) {
                need(
                        instruction.getOpcode() == Opcode.CONST_4
                                && instruction instanceof NarrowLiteralInstruction literal
                                && literal.getNarrowLiteral() == 0);
                falseDefinitions++;
            }

            if (instruction instanceof ReferenceInstruction ref
                    && ref.getReference().toString().equals(from)) {
                retargetCall(dex, instruction, address, falseDefinitions, output, reference);
                calls++;
            }

            address += instruction.getCodeUnits();
        }

        need(calls == 1 && address == 0xc8);
        var sha = MessageDigest.getInstance("SHA-1");
        sha.update(output, 32, output.length - 32);
        System.arraycopy(sha.digest(), 0, output, 12, 20);
        var checksum = new Adler32();
        checksum.update(output, 12, output.length - 12);
        ByteBuffer.wrap(output).order(ByteOrder.LITTLE_ENDIAN).putInt(8, (int) checksum.getValue());

        return output;
    }

    private static void retargetCall(
            DexBackedDexFile dex,
            Instruction instruction,
            int address,
            int falseDefinitions,
            byte[] output,
            int reference) {
        need(
                address == ADDRESS
                        && instruction.getOpcode() == Opcode.INVOKE_VIRTUAL
                        && instruction instanceof FiveRegisterInstruction
                        && instruction instanceof DexBackedInstruction);
        var registers = (FiveRegisterInstruction) instruction;
        need(
                registers.getRegisterCount() == 2
                        && registers.getRegisterC() == 6
                        && registers.getRegisterD() == 4
                        && falseDefinitions == 1);
        int operand =
                dex.getDataBuffer().getBaseOffset()
                        + ((DexBackedInstruction) instruction).instructionStart
                        + 2;
        need(operand >= 32 && operand + 1 < output.length);
        output[operand] = (byte) reference;
        output[operand + 1] = (byte) (reference >>> 8);
    }

    static byte[] apply(String profile, int api, Path original, byte[] dex) throws Exception {
        need(PROFILE.equals(profile) && api == 28 && APK_PIN.equals(FilesSupport.hash(original)));
        need(dex != null);
        byte[] output = rewrite(dex, false);
        need(Arrays.equals(dex, rewrite(output, true)));
        ViewDumpPatch.checkedDex(output, NativeCore.hash(output));

        return output;
    }

    static void verify(byte[] original, byte[] prepared) throws Exception {
        need(original != null && prepared != null);
        need(Arrays.equals(rewrite(original, false), prepared));
        need(Arrays.equals(original, rewrite(prepared, true)));
    }
}
