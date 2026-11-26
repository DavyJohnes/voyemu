package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.dexbacked.instruction.DexBackedInstruction;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.util.Arrays;

/** Keep the previous activity resumed when switching legacy activities, as in the OEM framework. */
final class LegacyActivityResumeGuard {
    private static final String PIN =
            "36ac3694c533ce4f1b115ee44972aec063ded8e23d76c7390627ec98544138ee";
    private static final String WM = "Lcom/android/server/wm/";
    private static final String TYPE = WM + "WindowProcessController;";
    private static final String ACTIVITY = WM + "ActivityRecord;";
    private static final String METHOD =
            TYPE + "->updateTopResumingActivityInProcessIfNeeded(" + ACTIVITY + ")Z";
    private static final int PAUSE_START = 0x53, PAUSE_END = 0x6a;

    private LegacyActivityResumeGuard() {}

    private static void require(boolean condition) {
        Failure.require(
                condition,
                "LEGACY_ACTIVITY_RESUME_CONTRACT",
                "Exact clean Android 11 legacy activity resume policy required",
                3);
    }

    private static int pauseBlock(Method method) {
        var code = method.getImplementation();

        require(code != null && code.getRegisterCount() == 8 && code.getTryBlocks().isEmpty());
        int offset = 0, start = -1, end = -1;

        for (var instruction : code.getInstructions()) {
            switch (offset) {
                case 0x51 ->
                        require(
                                instruction.getOpcode() == Opcode.IF_EQZ
                                        && ((OffsetInstruction) instruction).getCodeOffset()
                                                == 0x1b);
                case PAUSE_START -> start = ((DexBackedInstruction) instruction).instructionStart;
                case 0x67 ->
                        require(
                                instruction.getOpcode() == Opcode.INVOKE_VIRTUAL
                                        && instruction instanceof ReferenceInstruction call
                                        && call.getReference()
                                                .toString()
                                                .equals(
                                                        WM
                                                                + "ActivityStack;->startPausingLocked(ZZ"
                                                                + ACTIVITY
                                                                + ")Z"));
                case PAUSE_END -> {
                    require(
                            instruction.getOpcode() == Opcode.IPUT_OBJECT
                                    && instruction instanceof ReferenceInstruction store
                                    && store.getReference()
                                            .toString()
                                            .equals(
                                                    TYPE
                                                            + "->mPreQTopResumedActivity:"
                                                            + ACTIVITY));
                    end = ((DexBackedInstruction) instruction).instructionStart;
                }
                default -> {
                    // Other instructions only advance the code offset below.
                }
            }

            offset += instruction.getCodeUnits();
        }

        require(start >= 0 && end - start == 2 * (PAUSE_END - PAUSE_START) && offset == 111);

        return start;
    }

    static byte[] run(byte[] raw, boolean apply) throws Exception {
        require(raw != null && NativeCore.hash(raw).equals(PIN));
        var source = new DexBackedDexFile(Opcodes.forApi(30), raw);
        int start = -1;

        for (var type : source.getClasses()) {
            if (!type.getType().equals(TYPE)) {
                continue;
            }

            for (Method method : type.getVirtualMethods()) {
                if (DexEdits.descriptor(method).equals(METHOD)) {
                    require(start < 0);
                    start = source.getDataBuffer().getBaseOffset() + pauseBlock(method);
                }
            }
        }

        int end = start + 2 * (PAUSE_END - PAUSE_START);
        require(start >= 112 && end <= raw.length);

        if (!apply) {
            return raw;
        }

        // NOP the pause block while preserving branch addresses and all DEX metadata.
        byte[] output = raw.clone();
        Arrays.fill(output, start, end, (byte) 0);
        HiddenApiOffsets.checksum(output);
        ViewDumpPatch.checkedDex(output, NativeCore.hash(output));

        return output;
    }
}
