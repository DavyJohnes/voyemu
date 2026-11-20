package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.dexbacked.instruction.DexBackedInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.Map;
import java.util.zip.Adler32;

/** Return a stable non-null snapshot if disconnect clears the OEM call list concurrently. */
final class PhoneDisconnectGuard {
    static final String METHOD =
            "Lcom/qinggan/bluetoothphone/logic/manager/HeadSetProfileManager$HeadSetProfile;->getStateCalls()Ljava/util/List;";
    static final String FIELD =
            "Lcom/qinggan/bluetoothphone/logic/manager/HeadSetProfileManager$HeadSetProfile;->mLocalCurrentStateCalls:Ljava/util/List;";
    static final String LOG = "Landroid/util/Log;->i(Ljava/lang/String;Ljava/lang/String;)I";

    static void need(boolean ok) {
        Failure.require(ok, "PHONE_DISCONNECT_CONTRACT", "Exact call list getter required", 3);
    }

    static byte[] rewrite(byte[] source) throws Exception {
        var dex = new DexBackedDexFile(Opcodes.forApi(30), source);
        byte[] out = source.clone();
        int branch = -1, log = -1, field = -1, count = 0;

        for (var type : dex.getClasses()) {
            for (var method : type.getMethods()) {
                if (method.toString().equals(METHOD)) {
                    need(
                            ++count == 1
                                    && method.getImplementation() != null
                                    && method.getImplementation().getRegisterCount() == 5
                                    && method.getImplementation().getTryBlocks().isEmpty());
                    int address = 0;

                    for (var ins : method.getImplementation().getInstructions()) {
                        int offset =
                                dex.getDataBuffer().getBaseOffset()
                                        + ((DexBackedInstruction) ins).instructionStart;

                        if (address == 4) {
                            need(
                                    ins.getOpcode() == Opcode.NEW_INSTANCE
                                            && ((OneRegisterInstruction) ins).getRegisterA() == 0);
                        }

                        if (ins instanceof OffsetInstruction jump
                                && ins.getOpcode() == Opcode.IF_EQZ
                                && address + jump.getCodeOffset() == 0x5e) {
                            need(((OneRegisterInstruction) ins).getRegisterA() == 0 && branch < 0);
                            branch = offset;
                        }

                        if (address == 0x5b) {
                            log = offset;
                            need(isReference(ins, Opcode.INVOKE_STATIC, LOG));
                        }

                        if (address == 0x5e) {
                            field = offset;
                            need(isReference(ins, Opcode.IGET_OBJECT, FIELD));
                        }

                        address += ins.getCodeUnits();
                    }

                    need(address == 0x61);
                }
            }
        }

        need(count == 1 && branch >= 0 && log >= 0 && field == log + 6);
        // The old final log is omitted. Both paths enter this single field read.
        System.arraycopy(source, field, out, log, 4);
        out[log + 4] = 0x38;
        out[log + 5] = 0; // if-eqz v0, original empty ArrayList branch
        int delta = 4 - 0x5d;
        out[log + 6] = (byte) delta;
        out[log + 7] = (byte) (delta >>> 8);
        out[log + 8] = 0x11;
        out[log + 9] = 0; // return-object v0
        out[log + 10] = 0;
        out[log + 11] = 0;
        short old = ByteBuffer.wrap(source).order(ByteOrder.LITTLE_ENDIAN).getShort(branch + 2);
        ByteBuffer.wrap(out).order(ByteOrder.LITTLE_ENDIAN).putShort(branch + 2, (short) (old - 3));
        var sha = MessageDigest.getInstance("SHA-1");
        sha.update(out, 32, out.length - 32);
        System.arraycopy(sha.digest(), 0, out, 12, 20);
        var adler = new Adler32();
        adler.update(out, 12, out.length - 12);
        ByteBuffer.wrap(out).order(ByteOrder.LITTLE_ENDIAN).putInt(8, (int) adler.getValue());

        return out;
    }

    static boolean isReference(Instruction ins, Opcode opcode, String reference) {
        return ins.getOpcode() == opcode
                && ins instanceof ReferenceInstruction referenceInstruction
                && referenceInstruction.getReference().toString().equals(reference);
    }

    static byte[] apply(Map<String, Object> constants, int api, Path apk, byte[] dex)
            throws Exception {
        need(
                api == 30
                        && StepConstants.admits(constants, FilesSupport.hash(apk))
                        && dex != null
                        && Json.str(PhoneCallWindowGuard.policy(constants), "preparedDexSha256")
                                .equals(NativeCore.hash(dex)));
        byte[] out = rewrite(dex);
        ViewDumpPatch.checkedDex(out, NativeCore.hash(out));

        return out;
    }

    static void verify(Map<String, Object> constants, Path apk, byte[] original, byte[] prepared)
            throws Exception {
        byte[] window = PhoneCallWindowGuard.apply(constants, 30, apk, original);
        byte[] expected = apply(constants, 30, apk, window);

        need(Arrays.equals(expected, prepared));
        byte[] restored = prepared.clone();

        for (int i = 0; i < restored.length; i++) {
            if (window[i] != expected[i]) {
                restored[i] = window[i];
            }
        }

        need(Arrays.equals(restored, window));
        PhoneCallWindowGuard.verify(constants, original, restored);
    }
}
