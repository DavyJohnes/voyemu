package com.voyemu.prepare;

import org.jf.dexlib2.Format;
import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.ReferenceType;
import org.jf.dexlib2.builder.BuilderInstruction;
import org.jf.dexlib2.builder.Label;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11n;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction12x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction20t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21ih;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21lh;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21s;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22b;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22s;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction23x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction30t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction31c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction31i;
import org.jf.dexlib2.builder.instruction.BuilderInstruction32x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction3rc;
import org.jf.dexlib2.builder.instruction.BuilderInstruction51l;
import org.jf.dexlib2.formatter.DexFormatter;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.RegisterRangeInstruction;
import org.jf.dexlib2.iface.instruction.ThreeRegisterInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;
import org.jf.dexlib2.iface.instruction.WideLiteralInstruction;
import org.jf.dexlib2.iface.reference.Reference;
import org.jf.dexlib2.immutable.reference.ImmutableFieldReference;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;
import org.jf.dexlib2.immutable.reference.ImmutableStringReference;
import org.jf.dexlib2.immutable.reference.ImmutableTypeReference;

import java.util.ArrayList;
import java.util.List;
import java.util.function.Function;
import java.util.regex.Pattern;

/**
 * One DEX instruction as one line of smali syntax: registers as vN or pN, branch targets as labels
 * named by the instruction kind and the hexadecimal target address.
 */
final class DexText {
    private static final Opcodes OPCODES = Opcodes.forApi(28);
    private static final Pattern REGISTER = Pattern.compile("([vp])([0-9]+)");
    private static final Pattern LABEL = Pattern.compile(":[A-Za-z0-9_$]+");

    /** The register frame of one method: total registers and the trailing parameter registers. */
    record Frame(int registers, int parameters) {
        Frame {
            Failure.require(
                    parameters >= 0 && parameters <= registers && registers <= 65535,
                    "DEX_EDIT_INVALID",
                    "Invalid method register frame",
                    5);
        }

        String name(int register) {
            int first = registers - parameters;

            return register >= first ? "p" + (register - first) : "v" + register;
        }

        int register(String name) {
            var matcher = REGISTER.matcher(name);

            Failure.require(matcher.matches(), "DEX_EDIT_INVALID", "Invalid register: " + name, 5);
            int number = Integer.parseInt(matcher.group(2));
            int register = matcher.group(1).equals("p") ? registers - parameters + number : number;
            Failure.require(
                    matcher.group(1).equals("p") ? number < parameters : number < registers,
                    "DEX_EDIT_INVALID",
                    "Register outside method frame: " + name,
                    5);

            return register;
        }
    }

    private DexText() {}

    static String label(Opcode opcode, int target) {
        String prefix =
                switch (opcode) {
                    case GOTO, GOTO_16, GOTO_32 -> "goto_";
                    case PACKED_SWITCH -> "pswitch_data_";
                    case SPARSE_SWITCH -> "sswitch_data_";
                    case FILL_ARRAY_DATA -> "array_";
                    default -> "cond_";
                };

        return ":" + prefix + Integer.toHexString(target);
    }

    /** Renders an instruction at a code address of a method with the given frame. */
    static String render(Instruction instruction, int address, Frame frame) {
        Opcode opcode = instruction.getOpcode();

        Failure.require(
                !opcode.format.isPayloadFormat
                        && !opcode.odexOnly()
                        && opcode.format != Format.Format45cc
                        && opcode.format != Format.Format4rcc,
                "DEX_EDIT_UNSUPPORTED",
                "Unsupported anchor instruction: " + opcode.name,
                5);
        List<String> operands = new ArrayList<>();

        if (instruction instanceof RegisterRangeInstruction range) {
            int count = range.getRegisterCount(), start = range.getStartRegister();
            operands.add(
                    count == 0
                            ? "{}"
                            : "{"
                                    + frame.name(start)
                                    + " .. "
                                    + frame.name(start + count - 1)
                                    + "}");
        } else if (instruction instanceof FiveRegisterInstruction five) {
            int[] registers = {
                five.getRegisterC(),
                five.getRegisterD(),
                five.getRegisterE(),
                five.getRegisterF(),
                five.getRegisterG()
            };
            List<String> names = new ArrayList<>();

            for (int i = 0; i < five.getRegisterCount(); i++) {
                names.add(frame.name(registers[i]));
            }

            operands.add("{" + String.join(", ", names) + "}");
        } else {
            if (instruction instanceof OneRegisterInstruction one) {
                operands.add(frame.name(one.getRegisterA()));
            }

            if (instruction instanceof TwoRegisterInstruction two) {
                operands.add(frame.name(two.getRegisterB()));
            }

            if (instruction instanceof ThreeRegisterInstruction three) {
                operands.add(frame.name(three.getRegisterC()));
            }
        }

        if (instruction instanceof ReferenceInstruction reference) {
            operands.add(DexFormatter.INSTANCE.getReference(reference.getReference()));
        }

        if (instruction instanceof WideLiteralInstruction literal) {
            operands.add(literal(literal.getWideLiteral()));
        }

        if (instruction instanceof OffsetInstruction offset) {
            operands.add(label(opcode, address + offset.getCodeOffset()));
        }

        return operands.isEmpty() ? opcode.name : opcode.name + " " + String.join(", ", operands);
    }

    /** A literal as a signed hexadecimal int, or as a long with an L suffix when it needs one. */
    private static String literal(long value) {
        String suffix = value == (int) value ? "" : "L";

        return value < 0 && value != Long.MIN_VALUE
                ? "-0x" + Long.toHexString(-value) + suffix
                : "0x" + Long.toHexString(value) + suffix;
    }

    /** True when the line declares a label: a colon followed by the label name. */
    static boolean isLabel(String line) {
        return LABEL.matcher(line).matches();
    }

    /** Parses one instruction line; labels resolves every branch target. */
    static BuilderInstruction parse(String line, Frame frame, Function<String, Label> labels) {
        int space = line.indexOf(' ');
        String name = space < 0 ? line : line.substring(0, space);
        String rest = space < 0 ? "" : line.substring(space + 1);
        Opcode opcode = OPCODES.getOpcodeByName(name);

        Failure.require(
                opcode != null
                        && !opcode.odexOnly()
                        && !opcode.format.isPayloadFormat
                        && opcode.format != Format.Format45cc
                        && opcode.format != Format.Format4rcc,
                "DEX_EDIT_INVALID",
                "Unsupported instruction: " + line,
                5);
        var next = new Operands(operands(opcode, rest), line);

        try {
            BuilderInstruction result = build(opcode, next, frame, labels);
            next.end();

            return result;
        } catch (IllegalArgumentException error) {
            // dexlib2 rejects registers or literals that the instruction format cannot encode.
            throw new Failure("DEX_EDIT_INVALID", "Unencodable instruction: " + line, 5);
        }
    }

    private static BuilderInstruction build(
            Opcode opcode, Operands next, Frame frame, Function<String, Label> labels) {
        String line = next.line;
        BuilderInstruction result =
                switch (opcode.format) {
                    case Format10x -> new BuilderInstruction10x(opcode);
                    case Format10t -> new BuilderInstruction10t(opcode, labels.apply(next.next()));
                    case Format20t -> new BuilderInstruction20t(opcode, labels.apply(next.next()));
                    case Format30t -> new BuilderInstruction30t(opcode, labels.apply(next.next()));
                    case Format11n ->
                            new BuilderInstruction11n(
                                    opcode,
                                    frame.register(next.next()),
                                    (int) literal(next.next()));
                    case Format11x ->
                            new BuilderInstruction11x(opcode, frame.register(next.next()));
                    case Format12x ->
                            new BuilderInstruction12x(
                                    opcode,
                                    frame.register(next.next()),
                                    frame.register(next.next()));
                    case Format22x ->
                            new BuilderInstruction22x(
                                    opcode,
                                    frame.register(next.next()),
                                    frame.register(next.next()));
                    case Format32x ->
                            new BuilderInstruction32x(
                                    opcode,
                                    frame.register(next.next()),
                                    frame.register(next.next()));
                    case Format21c ->
                            new BuilderInstruction21c(
                                    opcode,
                                    frame.register(next.next()),
                                    reference(opcode, next.next()));
                    case Format31c ->
                            new BuilderInstruction31c(
                                    opcode,
                                    frame.register(next.next()),
                                    reference(opcode, next.next()));
                    case Format21ih ->
                            new BuilderInstruction21ih(
                                    opcode,
                                    frame.register(next.next()),
                                    (int) literal(next.next()));
                    case Format21lh ->
                            new BuilderInstruction21lh(
                                    opcode, frame.register(next.next()), literal(next.next()));
                    case Format21s ->
                            new BuilderInstruction21s(
                                    opcode,
                                    frame.register(next.next()),
                                    (int) literal(next.next()));
                    case Format31i ->
                            new BuilderInstruction31i(
                                    opcode,
                                    frame.register(next.next()),
                                    (int) literal(next.next()));
                    case Format51l ->
                            new BuilderInstruction51l(
                                    opcode, frame.register(next.next()), literal(next.next()));
                    case Format21t ->
                            new BuilderInstruction21t(
                                    opcode, frame.register(next.next()), labels.apply(next.next()));
                    case Format22t ->
                            new BuilderInstruction22t(
                                    opcode,
                                    frame.register(next.next()),
                                    frame.register(next.next()),
                                    labels.apply(next.next()));
                    case Format22b ->
                            new BuilderInstruction22b(
                                    opcode,
                                    frame.register(next.next()),
                                    frame.register(next.next()),
                                    (int) literal(next.next()));
                    case Format22s ->
                            new BuilderInstruction22s(
                                    opcode,
                                    frame.register(next.next()),
                                    frame.register(next.next()),
                                    (int) literal(next.next()));
                    case Format22c ->
                            new BuilderInstruction22c(
                                    opcode,
                                    frame.register(next.next()),
                                    frame.register(next.next()),
                                    reference(opcode, next.next()));
                    case Format23x ->
                            new BuilderInstruction23x(
                                    opcode,
                                    frame.register(next.next()),
                                    frame.register(next.next()),
                                    frame.register(next.next()));
                    case Format35c -> invoke(opcode, next, frame);
                    case Format3rc -> range(opcode, next, frame);
                    default ->
                            throw new Failure(
                                    "DEX_EDIT_INVALID", "Unsupported instruction: " + line, 5);
                };

        return result;
    }

    /** The operands of one instruction line, consumed in order. */
    private static final class Operands {
        private final List<String> values;
        private final String line;
        private int cursor;

        Operands(List<String> values, String line) {
            this.values = values;
            this.line = line;
        }

        String next() {
            Failure.require(
                    cursor < values.size(), "DEX_EDIT_INVALID", "Missing operand: " + line, 5);

            return values.get(cursor++);
        }

        void end() {
            Failure.require(
                    cursor == values.size(), "DEX_EDIT_INVALID", "Unexpected operand: " + line, 5);
        }
    }

    private static BuilderInstruction invoke(Opcode opcode, Operands next, Frame frame) {
        List<Integer> registers = list(next.next(), frame);

        Failure.require(
                registers.size() <= 5, "DEX_EDIT_INVALID", "Too many registers: " + next.line, 5);
        int[] values = new int[5];

        for (int i = 0; i < registers.size(); i++) {
            values[i] = registers.get(i);
        }

        return new BuilderInstruction35c(
                opcode,
                registers.size(),
                values[0],
                values[1],
                values[2],
                values[3],
                values[4],
                reference(opcode, next.next()));
    }

    private static BuilderInstruction range(Opcode opcode, Operands next, Frame frame) {
        String range = next.next();
        int start = 0, count = 0;

        if (!range.equals("{}")) {
            String[] bounds = range.substring(1, range.length() - 1).split(" \\.\\. ", -1);
            Failure.require(
                    range.startsWith("{") && range.endsWith("}") && bounds.length == 2,
                    "DEX_EDIT_INVALID",
                    "Invalid register range: " + next.line,
                    5);
            start = frame.register(bounds[0]);
            count = frame.register(bounds[1]) - start + 1;
        }

        return new BuilderInstruction3rc(opcode, start, count, reference(opcode, next.next()));
    }

    private static List<String> operands(Opcode opcode, String rest) {
        List<String> result = new ArrayList<>();

        if (rest.isEmpty()) {
            return result;
        }

        if (rest.startsWith("{")) {
            int close = rest.indexOf('}');
            Failure.require(close > 0, "DEX_EDIT_INVALID", "Unclosed register list", 5);
            result.add(rest.substring(0, close + 1));
            rest = rest.substring(close + 1);
            Failure.require(rest.startsWith(", "), "DEX_EDIT_INVALID", "Reference required", 5);
            result.add(rest.substring(2));

            return result;
        }

        // A string or type reference is always the last operand and may contain separators.
        boolean reference = opcode.referenceType != ReferenceType.NONE;
        String[] parts = rest.split(", ", -1);
        int count = reference ? opcode.format == Format.Format22c ? 3 : 2 : parts.length;

        for (int i = 0; i < count - 1 && i < parts.length; i++) {
            result.add(parts[i]);
        }

        int consumed = String.join(", ", result).length() + (result.isEmpty() ? 0 : 2);
        result.add(rest.substring(Math.min(consumed, rest.length())));

        return result;
    }

    private static List<Integer> list(String text, Frame frame) {
        Failure.require(
                text.startsWith("{") && text.endsWith("}"),
                "DEX_EDIT_INVALID",
                "Register list required",
                5);
        String inner = text.substring(1, text.length() - 1).trim();
        List<Integer> result = new ArrayList<>();

        if (!inner.isEmpty()) {
            for (String name : inner.split(", ", -1)) {
                result.add(frame.register(name));
            }
        }

        return result;
    }

    private static long literal(String text) {
        String value = text.endsWith("L") ? text.substring(0, text.length() - 1) : text;
        boolean negative = value.startsWith("-");
        String digits = negative ? value.substring(1) : value;

        try {
            long magnitude =
                    digits.startsWith("0x")
                            ? Long.parseUnsignedLong(digits.substring(2), 16)
                            : Long.parseLong(digits);

            return negative ? -magnitude : magnitude;
        } catch (NumberFormatException error) {
            throw new Failure("DEX_EDIT_INVALID", "Invalid literal: " + text, 5);
        }
    }

    private static Reference reference(Opcode opcode, String text) {
        return switch (opcode.referenceType) {
            case ReferenceType.STRING -> new ImmutableStringReference(string(text));
            case ReferenceType.TYPE -> new ImmutableTypeReference(type(text));
            case ReferenceType.FIELD -> {
                int arrow = text.indexOf("->"), colon = text.indexOf(':', arrow);
                Failure.require(
                        arrow > 0 && colon > arrow,
                        "DEX_EDIT_INVALID",
                        "Invalid field reference: " + text,
                        5);
                yield new ImmutableFieldReference(
                        type(text.substring(0, arrow)),
                        text.substring(arrow + 2, colon),
                        type(text.substring(colon + 1)));
            }
            case ReferenceType.METHOD -> method(text);
            default ->
                    throw new Failure(
                            "DEX_EDIT_INVALID", "Unsupported reference: " + opcode.name, 5);
        };
    }

    static ImmutableMethodReference method(String text) {
        int arrow = text.indexOf("->"), open = text.indexOf('(', arrow), close = text.indexOf(')');

        Failure.require(
                arrow > 0 && open > arrow && close > open,
                "DEX_EDIT_INVALID",
                "Invalid method reference: " + text,
                5);

        return new ImmutableMethodReference(
                type(text.substring(0, arrow)),
                text.substring(arrow + 2, open),
                types(text.substring(open + 1, close)),
                type(text.substring(close + 1)));
    }

    /** The parameter types of a descriptor parameter list such as {@code ILjava/lang/String;}. */
    static List<String> types(String text) {
        List<String> result = new ArrayList<>();

        for (int at = 0; at < text.length(); ) {
            int end = at;

            while (end < text.length() && text.charAt(end) == '[') {
                end++;
            }

            Failure.require(end < text.length(), "DEX_EDIT_INVALID", "Invalid type list", 5);

            if (text.charAt(end) == 'L') {
                end = text.indexOf(';', end);
                Failure.require(end > 0, "DEX_EDIT_INVALID", "Invalid type list", 5);
            }

            result.add(type(text.substring(at, end + 1)));
            at = end + 1;
        }

        return result;
    }

    private static String type(String text) {
        Failure.require(
                text.matches("\\[*(?:[VZBSCIJFD]|L[^;\\s]+;)"),
                "DEX_EDIT_INVALID",
                "Invalid type: " + text,
                5);

        return text;
    }

    private static String string(String text) {
        Failure.require(
                text.length() >= 2 && text.startsWith("\"") && text.endsWith("\""),
                "DEX_EDIT_INVALID",
                "Quoted string required",
                5);
        StringBuilder result = new StringBuilder();

        for (int i = 1; i < text.length() - 1; i++) {
            char c = text.charAt(i);

            if (c != '\\') {
                Failure.require(c != '"', "DEX_EDIT_INVALID", "Unescaped quote", 5);
                result.append(c);
                continue;
            }

            Failure.require(i + 1 < text.length() - 1, "DEX_EDIT_INVALID", "Invalid escape", 5);
            char escape = text.charAt(++i);

            switch (escape) {
                case 'n' -> result.append('\n');
                case 't' -> result.append('\t');
                case 'r' -> result.append('\r');
                case 'b' -> result.append('\b');
                case 'f' -> result.append('\f');
                case '"', '\'', '\\' -> result.append(escape);
                case 'u' -> {
                    Failure.require(
                            i + 4 < text.length() - 1, "DEX_EDIT_INVALID", "Invalid escape", 5);
                    result.append((char) Integer.parseInt(text.substring(i + 1, i + 5), 16));
                    i += 4;
                }
                default -> throw new Failure("DEX_EDIT_INVALID", "Invalid escape", 5);
            }
        }

        return result.toString();
    }
}
