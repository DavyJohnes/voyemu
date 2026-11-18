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
import java.util.List;
import java.util.Map;
import java.util.zip.Adler32;

/**
 * Exact profile fallback for the absent Island UI; retain OEM WeChat and explicit manual-minimum
 * branches.
 */
final class PhoneCallWindowGuard {
    record Variant(String apkPin, String dexPin, String outputPin) {}

    static final Map<String, Variant> VARIANTS =
            Map.of(
                    "free-11-chinese-20250710-6.11.1",
                    new Variant(
                            "c3b53187d5161692a338a9504e1fa288dd163896da431bf89b8ad1e8fa56b1f4",
                            "2cfb78adf260a790f7dce035b5572b13dd7a2885126c9c2b333a862e8908f490",
                            "e6bca757a16eb512230edc61d8368713e918c9b447e7813971dff6a07bfa31a5"),
                    "free-11-chinese-20260731-7.3.1",
                    new Variant(
                            "c8a33e377a6a708ef6c3cca7e8cc3f938349c04dd494dac32bcf823a3096efa1",
                            "e9e9710fc23ec92b9c1720c6ffbcf29c0bfdcd45c5bf8fa9a791846bd6d7c15b",
                            "f05d6bdbfe9b1399b2e4560f26760d3cff3c65f3bae1fbc05ee55a0947ef7496"));

    static List<String> pins(String profile) {
        var variant = VARIANTS.get(profile);

        return variant == null ? List.of() : List.of(variant.apkPin());
    }

    static Map<String, String> policy(String profile) {
        var variant = VARIANTS.get(profile);

        return variant == null
                ? Map.of()
                : Map.of(
                        "originalDexSha256",
                        variant.dexPin(),
                        "preparedDexSha256",
                        variant.outputPin(),
                        "automaticWindow",
                        "existing-oem-full-renderer",
                        "wechatAndManualMinimum",
                        "preserved");
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

    static byte[] apply(String profile, int api, Path original, byte[] dex) throws Exception {
        need(
                VARIANTS.containsKey(profile)
                        && api == 30
                        && VARIANTS.get(profile).apkPin().equals(FilesSupport.hash(original)));
        need(dex != null && VARIANTS.get(profile).dexPin().equals(NativeCore.hash(dex)));
        byte[] output = rewrite(dex, false);
        need(
                VARIANTS.get(profile).outputPin().equals(NativeCore.hash(output))
                        && Arrays.equals(dex, rewrite(output, true)));
        ViewDumpPatch.checkedDex(output, NativeCore.hash(output));

        return output;
    }

    static void validateOriginal(String profile, int api, Path original, byte[] dex)
            throws Exception {
        apply(profile, api, original, dex);
    }

    static void verify(String profile, byte[] original, byte[] prepared) throws Exception {
        need(
                VARIANTS.containsKey(profile)
                        && original != null
                        && VARIANTS.get(profile).dexPin().equals(NativeCore.hash(original))
                        && prepared != null);
        need(Arrays.equals(rewrite(original, false), prepared));
        need(Arrays.equals(original, rewrite(prepared, true)));
    }
}
