package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.dexbacked.DexBackedMethod;
import org.jf.dexlib2.dexbacked.OatFile;
import org.jf.dexlib2.dexbacked.instruction.DexBackedInstruction;

import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HexFormat;
import java.util.List;
import java.util.Map;

/**
 * Android9 VDEX019/002 metadata dequickening. AOSP android-9.0.0_r61: runtime/vdex_file.cc,
 * runtime/quicken_info.h, runtime/dex_to_dex_decompiler.cc, libdexfile/dex/compact_offset_table.cc.
 * Original access bits are never changed.
 */
final class VdexQuickening {
    private VdexQuickening() {}

    static void require(boolean condition) {
        Failure.require(
                condition,
                "FRAMEWORK_QUICKENING_INVALID",
                "Pinned Android9 VDEX quickening contract mismatch",
                5);
    }

    static void pin(byte[] bytes, String expected) throws Exception {
        require(
                expected != null
                        && expected.matches("[0-9a-f]{64}")
                        && HexFormat.of()
                                .formatHex(MessageDigest.getInstance("SHA-256").digest(bytes))
                                .equals(expected));
    }

    static int u16(byte[] b, int p) {
        require(p >= 0 && p <= b.length - 2);

        return (b[p] & 255) | ((b[p + 1] & 255) << 8);
    }

    static int u32(byte[] b, int p) {
        require(p >= 0 && p <= b.length - 4);
        long value =
                (b[p] & 255L)
                        | ((b[p + 1] & 255L) << 8)
                        | ((b[p + 2] & 255L) << 16)
                        | ((b[p + 3] & 255L) << 24);
        require(value <= Integer.MAX_VALUE);

        return (int) value;
    }

    static int add(int a, int b, int end) {
        require(a >= 0 && b >= 0 && a <= end && b <= end - a);

        return a + b;
    }

    static final class Leb {
        final byte[] bytes;
        final int end;
        int at;

        Leb(byte[] bytes, int at, int end) {
            require(at >= 0 && at <= end && end <= bytes.length);
            this.bytes = bytes;
            this.at = at;
            this.end = end;
        }

        int read() {
            long value = 0;

            for (int i = 0; i < 5; i++) {
                require(at < end);
                int current = bytes[at++] & 255;
                require(i < 4 || current < 16);
                value |= (long) (current & 127) << (i * 7);

                if ((current & 128) == 0) {
                    return (int) value;
                }
            }

            throw new Failure(
                    "FRAMEWORK_QUICKENING_INVALID",
                    "Pinned Android9 VDEX quickening contract mismatch",
                    5);
        }
    }

    record Section(int dex, int table, int methods, int fields, int types) {}

    record Layout(int quick, int end, List<Section> sections) {}

    static Layout layout(byte[] bytes) {
        require(
                bytes.length >= 36
                        && bytes.length <= 256 * 1024 * 1024
                        && Arrays.equals(
                                Arrays.copyOf(bytes, 12),
                                new byte[] {
                                    'v', 'd', 'e', 'x', '0', '1', '9', 0, '0', '0', '2', 0
                                }));
        int count = u32(bytes, 12);
        require(count > 0 && count <= 64);
        int header = add(20, count * 4, bytes.length);
        int dex = add(header, 12, bytes.length);
        int dexEnd = add(dex, u32(bytes, header), bytes.length);
        int sharedEnd = add(dexEnd, u32(bytes, header + 4), bytes.length);
        int quick = add(sharedEnd, u32(bytes, 16), bytes.length);
        int end = add(quick, u32(bytes, header + 8), bytes.length);
        require(end == bytes.length);
        List<Section> sections = new ArrayList<>();

        for (int i = 0; i < count; i++) {
            int table = u32(bytes, dex);
            dex = add(dex, 4, dexEnd);
            require(
                    dex <= dexEnd - 136
                            && Arrays.equals(
                                    Arrays.copyOfRange(bytes, dex, dex + 8),
                                    new byte[] {'c', 'd', 'e', 'x', '0', '0', '1', 0}));
            int size = u32(bytes, dex + 32);
            require(size >= 136);
            int next = add(dex, size, dexEnd);
            require(
                    add(dex, u32(bytes, dex + 108), sharedEnd) == dexEnd
                            && add(dexEnd, u32(bytes, dex + 104), sharedEnd) <= sharedEnd);
            sections.add(
                    new Section(
                            dex,
                            table,
                            u32(bytes, dex + 88),
                            u32(bytes, dex + 80),
                            u32(bytes, dex + 64)));
            dex = add(next, (4 - next % 4) % 4, dexEnd);
        }

        require(dex == dexEnd);

        return new Layout(quick, end, List.copyOf(sections));
    }

    static int offset(byte[] bytes, Layout layout, Section section, int index) {
        require(index >= 0 && index < section.methods());
        int start = add(layout.quick(), section.table(), layout.end());
        int minimum = u16(bytes, start) | (u16(bytes, start + 2) << 16),
                table = u32(bytes, add(start, 4, layout.end()));
        int data = add(start, 8, layout.end());
        int tableAt = add(data, table, layout.end());
        int blocks = (section.methods() + 15) / 16;
        require(blocks <= (layout.end() - tableAt) / 4);
        int block = add(data, u32(bytes, tableAt + index / 16 * 4), tableAt);
        require(block <= tableAt - 2);
        int mask = ((bytes[block] & 255) << 8) | (bytes[block + 1] & 255);
        int bit = index % 16;

        if ((mask & (1 << bit)) == 0) {
            return 0;
        }

        Leb leb = new Leb(bytes, block + 2, tableAt);
        int result = minimum;

        for (int n = Integer.bitCount(mask & ((1 << (bit + 1)) - 1)); n > 0; n--) {
            result += leb.read(); // AOSP uint32 deltas intentionally wrap for nonmonotonic offsets.
        }

        require(result > 0 && result <= layout.end() - layout.quick());

        return result;
    }

    static Opcode normal(Opcode opcode) {
        if (opcode == Opcode.INVOKE_VIRTUAL_QUICK) {
            return Opcode.INVOKE_VIRTUAL;
        }

        if (opcode == Opcode.INVOKE_VIRTUAL_QUICK_RANGE) {
            return Opcode.INVOKE_VIRTUAL_RANGE;
        }

        String name = opcode.name();

        if ((name.startsWith("IGET_") || name.startsWith("IPUT_")) && name.endsWith("_QUICK")) {
            try {
                return Opcode.valueOf(name.substring(0, name.length() - 6));
            } catch (IllegalArgumentException ignored) {
                throw new Failure(
                        "FRAMEWORK_QUICKENING_INVALID",
                        "Pinned Android9 VDEX quickening contract mismatch",
                        5);
            }
        }

        throw new Failure(
                "FRAMEWORK_QUICKENING_INVALID",
                "Pinned Android9 VDEX quickening contract mismatch",
                5);
    }

    static void put16(byte[] bytes, int at, int value) {
        require(value >= 0 && value <= 65535);
        u16(bytes, at);
        bytes[at] = (byte) value;
        bytes[at + 1] = (byte) (value >>> 8);
    }

    static byte[] decode(byte[] oatBytes, String oatPin, byte[] input, String vdexPin)
            throws Exception {
        pin(oatBytes, oatPin);
        pin(input, vdexPin);
        Layout layout = layout(input);
        byte[] decodedFlags = BootVdex.decode(input);
        OatFile oat = new OatFile(oatBytes, () -> decodedFlags);
        require(oat.getOatVersion() == 138 && oat.getDexFiles().size() == layout.sections().size());
        byte[] output = input.clone();
        int dexIndex = 0;

        for (DexBackedDexFile dex : oat.getDexFiles()) {
            Section section = layout.sections().get(dexIndex++);
            require(dex.getBuffer().getBaseOffset() == section.dex());
            Map<Integer, Integer> visited = new HashMap<>();

            for (var type : dex.getClasses()) {
                for (var method : type.getMethods()) {
                    decodeMethod(input, output, layout, section, dex, method, visited);
                }
            }
        }

        return output;
    }

    // Methods sharing one code item must share one quickening offset and are decoded once.
    static void decodeMethod(
            byte[] input,
            byte[] output,
            Layout layout,
            Section section,
            DexBackedDexFile dex,
            DexBackedMethod method,
            Map<Integer, Integer> visited) {
        var implementation = method.getImplementation();

        if (implementation == null) {
            return;
        }

        List<DexBackedInstruction> instructions = new ArrayList<>();

        for (var instruction : implementation.getInstructions()) {
            require(
                    instruction instanceof DexBackedInstruction
                            && !(instruction
                                    instanceof
                                    org.jf.dexlib2.iface.instruction.formats.UnknownInstruction));
            instructions.add((DexBackedInstruction) instruction);
        }

        require(!instructions.isEmpty());
        int methodIndex = method.getMethodIndex();
        int quickOffset = offset(input, layout, section, methodIndex);
        int codeKey = instructions.get(0).instructionStart;
        Integer prior = visited.putIfAbsent(codeKey, quickOffset);

        if (prior != null) {
            require(prior == quickOffset);

            return;
        }

        Quickening quickening = quickening(input, layout, quickOffset);
        int used =
                decodeInstructions(
                        input,
                        output,
                        dex.getDataBuffer().getBaseOffset(),
                        section,
                        implementation.getRegisterCount(),
                        instructions,
                        quickening);

        if (used != quickening.count()) {
            throw new Failure(
                    "FRAMEWORK_QUICKENING_INVALID",
                    "Quickening metadata consumption mismatch for " + method,
                    5);
        }

        require(quickening.indices() == null || quickening.end() <= layout.end());
    }

    record Quickening(Leb indices, int count, int end) {}

    static Quickening quickening(byte[] input, Layout layout, int quickOffset) {
        Leb indices =
                quickOffset == 0
                        ? null
                        : new Leb(
                                input,
                                add(layout.quick(), quickOffset - 1, layout.end()),
                                layout.end());
        int count = indices == null ? 0 : indices.read();

        require(count >= 0 && (indices == null || count <= (layout.end() - indices.at) / 2));
        int indicesEnd = indices == null ? 0 : add(indices.at, count * 2, layout.end());

        return new Quickening(indices, count, indicesEnd);
    }

    // Rewrites quickened instructions in output; returns the number of index entries consumed.
    static int decodeInstructions(
            byte[] input,
            byte[] output,
            int base,
            Section section,
            int registers,
            List<DexBackedInstruction> instructions,
            Quickening quickening) {
        Leb indices = quickening.indices();
        int count = quickening.count();
        int used = 0;

        for (int i = 0; i < instructions.size(); i++) {
            DexBackedInstruction instruction = instructions.get(i);
            int at = add(base, instruction.instructionStart, input.length);
            Opcode opcode = instruction.getOpcode();

            if (opcode == Opcode.RETURN_VOID_NO_BARRIER) {
                output[at] = (byte) 0x0e;
                continue;
            }

            if ((opcode == Opcode.NOP
                            || opcode == Opcode.ARRAY_PAYLOAD
                            || opcode == Opcode.PACKED_SWITCH_PAYLOAD
                            || opcode == Opcode.SPARSE_SWITCH_PAYLOAD)
                    && count > 0) {
                require(used < count);
                int register = u16(input, indices.at + used++ * 2);

                if (register == 65535) {
                    continue;
                }

                require(opcode == Opcode.NOP);
                require(
                        register < registers
                                && register <= 255
                                && used < count
                                && i + 1 < instructions.size());
                int typeIndex = u16(input, indices.at + used++ * 2);
                require(typeIndex < section.types());
                DexBackedInstruction next = instructions.get(++i);
                require(
                        next.getOpcode() == Opcode.NOP
                                && next.instructionStart == instruction.instructionStart + 2
                                && next.getCodeUnits() == 1);
                put16(output, at, 0x1f | (register << 8));
                put16(output, at + 2, typeIndex);
            } else if (opcode.odexOnly()) {
                Opcode replacement = normal(opcode);
                require(used < count && indices != null);
                int reference = u16(input, indices.at + used++ * 2);
                require(
                        reference
                                < (replacement.name().startsWith("INVOKE_")
                                        ? section.methods()
                                        : section.fields()));
                Short value = Opcodes.forApi(28).getOpcodeValue(replacement);
                require(value != null && (value & 0xffff) <= 255);
                output[at] = (byte) value.shortValue();
                put16(output, at + 2, reference);
            }
        }

        return used;
    }
}
