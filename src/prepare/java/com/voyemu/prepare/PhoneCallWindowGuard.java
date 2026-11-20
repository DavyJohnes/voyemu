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
import java.util.Map;
import java.util.zip.Adler32;

/**
 * Exact profile fallback for the absent Island UI; retain OEM WeChat and explicit manual-minimum
 * branches.
 */
final class PhoneCallWindowGuard {
    /**
     * The window policy of the step: the pins of the original and prepared DEX and the retained OEM
     * behavior.
     */
    static Map<String, Object> policy(Map<String, Object> constants) {
        Map<String, Object> policy = Json.obj(constants, "windowPolicy");

        need(
                policy.keySet()
                                .equals(
                                        java.util.Set.of(
                                                "originalDexSha256",
                                                "preparedDexSha256",
                                                "automaticWindow",
                                                "wechatAndManualMinimum"))
                        && Json.str(policy, "originalDexSha256").matches("[0-9a-f]{64}")
                        && Json.str(policy, "preparedDexSha256").matches("[0-9a-f]{64}")
                        && "existing-oem-full-renderer".equals(policy.get("automaticWindow"))
                        && "preserved".equals(policy.get("wechatAndManualMinimum")));

        return policy;
    }

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
                "PHONE_CALL_WINDOW_CONTRACT",
                "Exact pinned automatic Phone window call required",
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
        var dex = new DexBackedDexFile(Opcodes.forApi(30), source);
        String from = inverse ? NEW : OLD, to = inverse ? OLD : NEW;
        int reference = methodIndex(dex, to);
        byte[] output = source.clone();
        int address = 0, calls = 0, falseDefinitions = 0, foreground = 0;

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

            if (address == 0x97) {
                rewriteForeground(dex, instruction, output, inverse);
                foreground++;
            }

            if (instruction instanceof ReferenceInstruction ref
                    && ref.getReference().toString().equals(from)) {
                retargetCall(dex, instruction, address, falseDefinitions, output, reference);
                calls++;
            }

            address += instruction.getCodeUnits();
        }

        need(calls == 1 && foreground == 1 && address == 0xc8);
        var sha = MessageDigest.getInstance("SHA-1");
        sha.update(output, 32, output.length - 32);
        System.arraycopy(sha.digest(), 0, output, 12, 20);
        var checksum = new Adler32();
        checksum.update(output, 12, output.length - 12);
        ByteBuffer.wrap(output).order(ByteOrder.LITTLE_ENDIAN).putInt(8, (int) checksum.getValue());

        return output;
    }

    private static void rewriteForeground(
            DexBackedDexFile dex, Instruction instruction, byte[] output, boolean inverse) {
        need(instruction instanceof DexBackedInstruction);
        int offset =
                dex.getDataBuffer().getBaseOffset()
                        + ((DexBackedInstruction) instruction).instructionStart;

        if (inverse) {
            need(
                    instruction.getOpcode() == Opcode.NOP
                            && output[offset + 1] == 0
                            && output[offset + 2] == 0
                            && output[offset + 3] == 0);
            output[offset] = 0x5c;
            output[offset + 1] = 0x64;
            // The original exact field index is read from the pinned original, never
            // guessed.
            var fields = dex.getFieldSection();
            int field = -1;

            for (int index = 0; index < fields.size(); index++) {
                if (fields.get(index).toString().equals(OWNER + "->isFullScreen:Z")) {
                    field = index;
                }
            }

            need(field >= 0 && field <= 65535);
            output[offset + 2] = (byte) field;
            output[offset + 3] = (byte) (field >>> 8);
        } else {
            need(
                    instruction.getOpcode() == Opcode.IPUT_BOOLEAN
                            && instruction
                                    instanceof
                                    org.jf.dexlib2.iface.instruction.TwoRegisterInstruction);
            var registers = (org.jf.dexlib2.iface.instruction.TwoRegisterInstruction) instruction;
            need(
                    registers.getRegisterA() == 4
                            && registers.getRegisterB() == 6
                            && instruction instanceof ReferenceInstruction referenceInstruction
                            && referenceInstruction
                                    .getReference()
                                    .toString()
                                    .equals(OWNER + "->isFullScreen:Z"));
            Arrays.fill(output, offset, offset + 4, (byte) 0);
        }
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

    static byte[] apply(Map<String, Object> constants, int api, Path original, byte[] dex)
            throws Exception {
        Map<String, Object> policy = policy(constants);

        need(api == 30 && StepConstants.admits(constants, FilesSupport.hash(original)));
        need(dex != null && Json.str(policy, "originalDexSha256").equals(NativeCore.hash(dex)));
        byte[] output = rewrite(dex, false);
        need(
                Json.str(policy, "preparedDexSha256").equals(NativeCore.hash(output))
                        && Arrays.equals(dex, rewrite(output, true)));
        ViewDumpPatch.checkedDex(output, NativeCore.hash(output));

        return output;
    }

    static void validateOriginal(Map<String, Object> constants, int api, Path original, byte[] dex)
            throws Exception {
        apply(constants, api, original, dex);
    }

    static void verify(Map<String, Object> constants, byte[] original, byte[] prepared)
            throws Exception {
        need(
                original != null
                        && Json.str(policy(constants), "originalDexSha256")
                                .equals(NativeCore.hash(original))
                        && prepared != null);
        need(Arrays.equals(rewrite(original, false), prepared));
        need(Arrays.equals(original, rewrite(prepared, true)));
    }
}
