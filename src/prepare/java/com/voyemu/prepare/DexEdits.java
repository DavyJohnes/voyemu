package com.voyemu.prepare;

import org.jf.dexlib2.Format;
import org.jf.dexlib2.builder.BuilderInstruction;
import org.jf.dexlib2.builder.BuilderOffsetInstruction;
import org.jf.dexlib2.builder.BuilderSwitchPayload;
import org.jf.dexlib2.builder.BuilderTryBlock;
import org.jf.dexlib2.builder.Label;
import org.jf.dexlib2.builder.MethodLocation;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10x;
import org.jf.dexlib2.formatter.DexFormatter;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.MethodImplementation;
import org.jf.dexlib2.iface.debug.DebugItem;
import org.jf.dexlib2.iface.debug.EndLocal;
import org.jf.dexlib2.iface.debug.RestartLocal;
import org.jf.dexlib2.iface.debug.StartLocal;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.RegisterRangeInstruction;
import org.jf.dexlib2.iface.instruction.ThreeRegisterInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;
import org.jf.dexlib2.iface.instruction.WideLiteralInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.debug.ImmutableDebugItem;
import org.jf.dexlib2.immutable.debug.ImmutableEndLocal;
import org.jf.dexlib2.immutable.debug.ImmutableRestartLocal;
import org.jf.dexlib2.immutable.debug.ImmutableStartLocal;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction11n;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction11x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction12x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction21c;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction21ih;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction21lh;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction21s;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction21t;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction22b;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction22c;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction22s;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction22t;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction22x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction23x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction31c;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction31i;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction31t;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction32x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction35c;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction3rc;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction51l;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.function.IntUnaryOperator;
import java.util.regex.Pattern;

/**
 * Method-level edits of DEX code: each edit finds one exact instruction sequence in the original
 * method and replaces it, without a disassembly round trip of the class or the DEX.
 */
final class DexEdits {
    private static final Pattern CATCH =
            Pattern.compile(
                    "\\.catch(?:all| (\\S+)) \\{(:[A-Za-z0-9_$]+) \\.\\. (:[A-Za-z0-9_$]+)\\}"
                            + " (:[A-Za-z0-9_$]+)");

    /**
     * One edit: anchor is an exact instruction sequence matching once in the original method;
     * delete instructions starting at offset are removed and insert takes their place. Labels on
     * the first removed instruction move to the inserted code; with nothing removed, moveLabels
     * makes the labels of the instruction at offset target the inserted code.
     */
    record Edit(
            List<String> anchor, int offset, int delete, List<String> insert, boolean moveLabels) {
        /** Removes the instructions from the offset to the end of the method. */
        static final int TAIL = Integer.MAX_VALUE;

        Edit {
            Failure.require(
                    !anchor.isEmpty()
                            && offset >= 0
                            && delete >= 0
                            && (delete == TAIL || offset + delete <= anchor.size())
                            && (delete > 0 || !insert.isEmpty())
                            && (!moveLabels || offset < anchor.size()),
                    "DEX_EDIT_INVALID",
                    "Invalid method edit",
                    5);
            anchor = List.copyOf(anchor);
            insert = List.copyOf(insert);
        }

        /** Replaces find by with; identical leading and trailing instructions are context. */
        static Edit replace(List<String> find, List<String> with) {
            int prefix = 0, suffix = 0;

            while (prefix < find.size()
                    && prefix < with.size()
                    && find.get(prefix).equals(with.get(prefix))) {
                prefix++;
            }

            while (suffix < find.size() - prefix
                    && suffix < with.size() - prefix
                    && find.get(find.size() - 1 - suffix)
                            .equals(with.get(with.size() - 1 - suffix))) {
                suffix++;
            }

            return new Edit(
                    find,
                    prefix,
                    find.size() - prefix - suffix,
                    with.subList(prefix, with.size() - suffix),
                    false);
        }

        static Edit before(List<String> anchor, List<String> insert) {
            return new Edit(anchor, 0, 0, insert, true);
        }

        static Edit after(List<String> anchor, List<String> insert) {
            return new Edit(anchor, anchor.size(), 0, insert, false);
        }

        /** Replaces the instructions from the anchor to the end of the method. */
        static Edit tail(List<String> anchor, List<String> with) {
            return new Edit(anchor, 0, TAIL, with, false);
        }
    }

    /**
     * Edits of one method: its descriptor, an optional new register count and the edits, or a
     * register count and a body that replaces the code, its try blocks and debug items.
     */
    record MethodEdit(String method, Integer registers, List<Edit> edits, List<String> body) {
        MethodEdit {
            Failure.require(
                    method.matches("L[^;\\s]+;->[^(\\s]+\\([^)\\s]*\\)\\S+")
                            && (registers == null || (registers >= 0 && registers <= 65535))
                            && (body == null
                                    ? registers != null || !edits.isEmpty()
                                    : registers != null && edits.isEmpty() && !body.isEmpty()),
                    "DEX_EDIT_INVALID",
                    "Invalid method edit",
                    5);
            edits = List.copyOf(edits);
            body = body == null ? null : List.copyOf(body);
        }

        MethodEdit(String method, Integer registers, List<Edit> edits) {
            this(method, registers, edits, null);
        }

        String type() {
            return method.substring(0, method.indexOf("->"));
        }

        /** Reads {@code ["Lc;->m()V", {"registers": n, "edits": [...]}]}. */
        static MethodEdit parse(List<Object> args) {
            Failure.require(
                    args.size() == 2 && args.get(0) instanceof String,
                    "DEX_EDIT_INVALID",
                    "Method and edits required",
                    5);
            Map<String, Object> spec = Json.object(args.get(1));
            Failure.require(
                    Set.of("registers", "edits", "body").containsAll(spec.keySet()),
                    "DEX_EDIT_INVALID",
                    "Unknown method edit field",
                    5);
            List<Edit> edits = new ArrayList<>();

            for (Object item :
                    spec.containsKey("edits") ? Json.list(spec.get("edits")) : List.of()) {
                Map<String, Object> edit = Json.object(item);

                if (edit.keySet().equals(Set.of("replace", "with"))) {
                    edits.add(Edit.replace(lines(edit.get("replace")), lines(edit.get("with"))));
                } else if (edit.keySet().equals(Set.of("tail", "with"))) {
                    edits.add(Edit.tail(lines(edit.get("tail")), lines(edit.get("with"))));
                } else if (edit.keySet().equals(Set.of("before", "insert"))) {
                    edits.add(Edit.before(lines(edit.get("before")), lines(edit.get("insert"))));
                } else {
                    Failure.require(
                            edit.keySet().equals(Set.of("after", "insert")),
                            "DEX_EDIT_INVALID",
                            "Edit needs replace/with, tail/with, before/insert or after/insert",
                            5);
                    edits.add(Edit.after(lines(edit.get("after")), lines(edit.get("insert"))));
                }
            }

            return new MethodEdit(
                    (String) args.get(0),
                    spec.containsKey("registers")
                            ? Math.toIntExact(Json.integer(spec, "registers"))
                            : null,
                    edits,
                    spec.containsKey("body") ? lines(spec.get("body")) : null);
        }

        private static List<String> lines(Object value) {
            List<String> result = new ArrayList<>();

            for (Object line : Json.list(value)) {
                Failure.require(
                        line instanceof String text && !text.isBlank() && text.equals(text.strip()),
                        "DEX_EDIT_INVALID",
                        "Instruction line required",
                        5);
                result.add((String) line);
            }

            return result;
        }
    }

    private DexEdits() {}

    /** The descriptor of a method: {@code Lclass;->name(parameters)return}. */
    static String descriptor(Method method) {
        return DexFormatter.INSTANCE.getMethodDescriptor(method);
    }

    /** Registers that hold the parameters, including this for an instance method. */
    static int parameters(Method method) {
        int count = (method.getAccessFlags() & 0x8) == 0 ? 1 : 0;

        for (CharSequence type : method.getParameterTypes()) {
            count += type.charAt(0) == 'J' || type.charAt(0) == 'D' ? 2 : 1;
        }

        return count;
    }

    /** The instructions of a method as smali lines, with code addresses as label names. */
    static List<String> render(MethodImplementation code, DexText.Frame frame) {
        List<String> result = new ArrayList<>();
        int address = 0;

        for (Instruction instruction : code.getInstructions()) {
            if (!instruction.getOpcode().format.isPayloadFormat) {
                result.add(DexText.render(instruction, address, frame));
            } else {
                result.add("." + instruction.getOpcode().name);
            }

            address += instruction.getCodeUnits();
        }

        return result;
    }

    /** Applies the edits of one class; every edited method must exist exactly once. */
    static ClassDef apply(ClassDef type, List<MethodEdit> edits) {
        Map<String, MethodEdit> byMethod = new LinkedHashMap<>();

        for (MethodEdit edit : edits) {
            Failure.require(
                    edit.type().equals(type.getType())
                            && byMethod.putIfAbsent(edit.method(), edit) == null,
                    "DEX_EDIT_INVALID",
                    "One edit per method of the selected class required",
                    5);
        }

        List<Method> methods = new ArrayList<>();

        for (Method method : type.getMethods()) {
            MethodEdit edit = byMethod.remove(descriptor(method));
            methods.add(edit == null ? method : apply(method, edit));
        }

        Failure.require(
                byMethod.isEmpty(),
                "DEX_EDIT_ANCHOR",
                "Edited method absent: " + String.join(", ", byMethod.keySet()),
                5);

        return new ImmutableClassDef(
                type.getType(),
                type.getAccessFlags(),
                type.getSuperclass(),
                type.getInterfaces(),
                type.getSourceFile(),
                type.getAnnotations(),
                type.getFields(),
                methods);
    }

    /** Adds an interface after the interfaces the class implements. */
    static ClassDef addInterface(ClassDef type, String name) {
        List<String> interfaces = new ArrayList<>(type.getInterfaces());

        Failure.require(
                !interfaces.contains(name), "DEX_EDIT_INVALID", "Interface already present", 5);
        interfaces.add(name);

        return new ImmutableClassDef(
                type.getType(),
                type.getAccessFlags(),
                type.getSuperclass(),
                interfaces,
                type.getSourceFile(),
                type.getAnnotations(),
                type.getFields(),
                type.getMethods());
    }

    /**
     * Adds a field without an initial value, or with flags present replaces the access flags of an
     * existing field of that name and type.
     */
    static ClassDef field(ClassDef type, String name, String fieldType, int flags, boolean add) {
        List<org.jf.dexlib2.iface.Field> fields = new ArrayList<>();
        boolean found = false;

        for (var field : type.getFields()) {
            if (field.getName().equals(name)) {
                Failure.require(
                        !add && !found && field.getType().equals(fieldType),
                        "DEX_EDIT_ANCHOR",
                        "Field " + name + " differs from the edit",
                        5);
                found = true;
                fields.add(
                        new org.jf.dexlib2.immutable.ImmutableField(
                                field.getDefiningClass(),
                                name,
                                fieldType,
                                flags,
                                field.getInitialValue(),
                                field.getAnnotations(),
                                field.getHiddenApiRestrictions()));
            } else {
                fields.add(field);
            }
        }

        Failure.require(
                found != add, "DEX_EDIT_ANCHOR", "Field " + name + " differs from the edit", 5);

        if (add) {
            fields.add(
                    new org.jf.dexlib2.immutable.ImmutableField(
                            type.getType(), name, fieldType, flags, null, null, null));
        }

        return new ImmutableClassDef(
                type.getType(),
                type.getAccessFlags(),
                type.getSuperclass(),
                type.getInterfaces(),
                type.getSourceFile(),
                type.getAnnotations(),
                fields,
                type.getMethods());
    }

    /** Adds a method that the class does not define, with code given as smali lines. */
    static ClassDef addMethod(
            ClassDef type, String descriptor, int flags, int registers, List<String> lines) {
        String method = type.getType() + "->" + descriptor;
        var reference = DexText.method(method);

        for (Method existing : type.getMethods()) {
            Failure.require(
                    !descriptor(existing).equals(method),
                    "DEX_EDIT_INVALID",
                    "Method already present: " + method,
                    5);
        }

        List<org.jf.dexlib2.iface.MethodParameter> parameters = new ArrayList<>();

        for (String parameter : reference.getParameterTypes()) {
            parameters.add(
                    new org.jf.dexlib2.immutable.ImmutableMethodParameter(parameter, null, null));
        }

        var empty = new MutableMethodImplementation(registers);
        empty.addInstruction(new BuilderInstruction10x(org.jf.dexlib2.Opcode.NOP));
        Method added =
                apply(
                        new ImmutableMethod(
                                type.getType(),
                                reference.getName(),
                                parameters,
                                reference.getReturnType(),
                                flags,
                                null,
                                null,
                                empty),
                        new MethodEdit(method, null, List.of(Edit.replace(List.of("nop"), lines))));
        List<Method> methods = new ArrayList<>();

        for (Method existing : type.getMethods()) {
            methods.add(existing);
        }

        methods.add(added);

        return new ImmutableClassDef(
                type.getType(),
                type.getAccessFlags(),
                type.getSuperclass(),
                type.getInterfaces(),
                type.getSourceFile(),
                type.getAnnotations(),
                type.getFields(),
                methods);
    }

    static Method apply(Method method, MethodEdit edit) {
        MethodImplementation original = method.getImplementation();

        Failure.require(
                original != null && descriptor(method).equals(edit.method()),
                "DEX_EDIT_ANCHOR",
                "Edited method has no code: " + edit.method(),
                5);

        if (edit.body() != null) {
            return replaceBody(method, edit);
        }

        int parameters = parameters(method);
        int count = edit.registers() == null ? original.getRegisterCount() : edit.registers();
        Code code =
                new Code(
                        registers(original, count, parameters),
                        new DexText.Frame(count, parameters),
                        edit.method());
        code.apply(edit.edits());

        return new ImmutableMethod(
                method.getDefiningClass(),
                method.getName(),
                method.getParameters(),
                method.getReturnType(),
                method.getAccessFlags(),
                method.getAnnotations(),
                method.getHiddenApiRestrictions(),
                code.result());
    }

    /** Replaces the code, try blocks and debug items, parameter names included, of a method. */
    private static Method replaceBody(Method method, MethodEdit edit) {
        var empty = new MutableMethodImplementation(edit.registers());

        empty.addInstruction(new BuilderInstruction10x(org.jf.dexlib2.Opcode.NOP));
        List<org.jf.dexlib2.iface.MethodParameter> parameters = new ArrayList<>();

        for (var parameter : method.getParameters()) {
            parameters.add(
                    new org.jf.dexlib2.immutable.ImmutableMethodParameter(
                            parameter.getType(), parameter.getAnnotations(), null));
        }

        return apply(
                new ImmutableMethod(
                        method.getDefiningClass(),
                        method.getName(),
                        parameters,
                        method.getReturnType(),
                        method.getAccessFlags(),
                        method.getAnnotations(),
                        method.getHiddenApiRestrictions(),
                        empty),
                new MethodEdit(
                        edit.method(), null, List.of(Edit.replace(List.of("nop"), edit.body()))));
    }

    /** Inserted instruction lines, their placeholder locations and catch directives. */
    private record Inserted(
            List<String> instructions,
            List<MethodLocation> locations,
            List<java.util.regex.Matcher> catches) {}

    /** Smali lines of an inserted block: instructions, labels, catches and line numbers. */
    private record Insertion(
            List<String> instructions,
            Map<String, Integer> labels,
            List<java.util.regex.Matcher> catches,
            Map<Integer, Integer> lineNumbers) {
        static Insertion of(List<String> lines) {
            Insertion result =
                    new Insertion(
                            new ArrayList<>(),
                            new LinkedHashMap<>(),
                            new ArrayList<>(),
                            new HashMap<>());

            for (String line : lines) {
                int position = result.instructions().size();

                if (line.matches("\\.line [0-9]+")) {
                    Failure.require(
                            result.lineNumbers().put(position, Integer.parseInt(line.substring(6)))
                                    == null,
                            "DEX_EDIT_INVALID",
                            "Duplicate line number",
                            5);
                } else if (DexText.isLabel(line)) {
                    Failure.require(
                            result.labels().put(line, position) == null,
                            "DEX_EDIT_INVALID",
                            "Duplicate label: " + line,
                            5);
                } else if (line.startsWith(".catch")) {
                    var matcher = CATCH.matcher(line);
                    Failure.require(
                            matcher.matches(), "DEX_EDIT_INVALID", "Invalid catch: " + line, 5);
                    result.catches().add(matcher);
                } else {
                    result.instructions().add(line);
                }
            }

            return result;
        }
    }

    /** One method body under edit: mutable code, original locations and the labels of edits. */
    private static final class Code {
        private final MutableMethodImplementation mutable;
        private final DexText.Frame frame;
        private final String method;
        private final List<String> lines;
        private final List<MethodLocation> locations = new ArrayList<>();
        private final Map<String, MethodLocation> existing = new HashMap<>();
        // Labels declared by inserted code are visible to every edit of the method.
        private final Map<String, MethodLocation> declared = new HashMap<>();
        private final Map<MethodLocation, Label> labels = new HashMap<>();
        private final Set<BuilderTryBlock> removedTries = new java.util.HashSet<>();
        private final List<Inserted> inserted = new ArrayList<>();

        Code(MethodImplementation code, DexText.Frame frame, String method) {
            this.frame = frame;
            this.method = method;
            lines = render(code, frame);
            mutable = new MutableMethodImplementation(code);
            int address = 0, index = 0;

            for (BuilderInstruction instruction : mutable.getInstructions()) {
                locations.add(instruction.getLocation());
            }

            for (Instruction instruction : code.getInstructions()) {
                existing.put(Integer.toHexString(address), locations.get(index++));
                address += instruction.getCodeUnits();
            }
        }

        /** Matches every anchor in the original method, then edits from the last to the first. */
        void apply(List<Edit> edits) {
            TreeMap<Integer, Edit> positions = new TreeMap<>(Comparator.reverseOrder());

            for (Edit change : edits) {
                int at = match(lines, change.anchor(), method);
                Failure.require(
                        positions.put(at + change.offset(), change) == null,
                        "DEX_EDIT_OVERLAP",
                        "Overlapping method edits: " + method,
                        5);
            }

            int previous = Integer.MAX_VALUE;

            for (var entry : positions.entrySet()) {
                int start = entry.getKey();
                Edit change = entry.getValue();
                int delete = change.delete() == Edit.TAIL ? lines.size() - start : change.delete();
                Failure.require(
                        start + delete <= previous,
                        "DEX_EDIT_OVERLAP",
                        "Overlapping method edits: " + method,
                        5);
                previous = start;
                Failure.require(
                        start < locations.size(),
                        "DEX_EDIT_INVALID",
                        "Insertion after the last instruction of " + method,
                        5);
                remove(start, delete);
                inserted.add(insert(locations.get(start).getIndex(), delete, change));
            }

            for (Inserted item : inserted) {
                for (int i = 0; i < item.instructions().size(); i++) {
                    mutable.replaceInstruction(
                            item.locations().get(i).getIndex(),
                            DexText.parse(item.instructions().get(i), frame, this::resolve));
                }

                for (var matcher : item.catches()) {
                    Label from = resolve(matcher.group(2)),
                            to = resolve(matcher.group(3)),
                            handler = resolve(matcher.group(4));

                    if (matcher.group(1) == null) {
                        mutable.addCatch(from, to, handler);
                    } else {
                        mutable.addCatch(matcher.group(1), from, to, handler);
                    }
                }
            }
        }

        MethodImplementation result() {
            return new ImmutableMethodImplementation(
                    frame.registers(),
                    mutable.getInstructions(),
                    org.jf.dexlib2.writer.util.TryListBuilder.massageTryBlocks(
                            mutable.getTryBlocks().stream()
                                    .filter(block -> !removedTries.contains(block))
                                    .toList()),
                    mutable.getDebugItems());
        }

        /** Labels that instructions or try blocks outside the removed locations reference. */
        private Set<Label> referenced(Set<MethodLocation> removed, int start, int delete) {
            Set<Label> result = new java.util.HashSet<>();

            for (BuilderInstruction instruction : mutable.getInstructions()) {
                if (removed.contains(instruction.getLocation())) {
                    continue;
                }

                if (instruction instanceof BuilderOffsetInstruction offset) {
                    result.add(offset.getTarget());
                }

                if (instruction instanceof BuilderSwitchPayload payload) {
                    for (var element : payload.getSwitchElements()) {
                        result.add(element.getTarget());
                    }
                }
            }

            // A try block that starts and ends inside the removed instructions goes with them.
            Set<MethodLocation> interior =
                    new java.util.HashSet<>(
                            locations.subList(start + Math.min(1, delete), start + delete));

            for (var block : mutable.getTryBlocks()) {
                if (removedTries.contains(block)) {
                    continue;
                }

                boolean inside = interior.contains(block.end.getLocation());
                Failure.require(
                        inside == removed.contains(block.start.getLocation())
                                && (!inside
                                        || removed.contains(
                                                block.exceptionHandler.getHandler().getLocation())),
                        "DEX_EDIT_ANCHOR",
                        "Edit removes part of a try block in " + method,
                        5);

                if (inside) {
                    removedTries.add(block);
                } else {
                    result.add(block.start);
                    result.add(block.end);
                    result.add(block.exceptionHandler.getHandler());
                }
            }

            return result;
        }

        /** Removes the instructions after the first replaced one; their labels must be unused. */
        private void remove(int start, int delete) {
            Set<Label> referenced =
                    referenced(
                            new java.util.HashSet<>(locations.subList(start, start + delete)),
                            start,
                            delete);

            for (int i = start + 1; i < start + delete; i++) {
                for (Label label : locations.get(i).getLabels()) {
                    Failure.require(
                            !referenced.contains(label),
                            "DEX_EDIT_ANCHOR",
                            "Edit removes a branch or try target in " + method,
                            5);
                }

                // Debug items between removed instructions go with them.
                locations.get(i).getDebugItems().clear();
            }

            int index = locations.get(start).getIndex();

            for (int i = start + delete - 1; i > start; i--) {
                mutable.removeInstruction(index + 1);
            }
        }

        /** Places one placeholder per inserted instruction at index, over the replaced one. */
        private Inserted insert(int index, int delete, Edit change) {
            Insertion insertion = Insertion.of(change.insert());
            List<String> instructions = insertion.instructions();

            if (delete > 0 && instructions.isEmpty()) {
                mutable.removeInstruction(index);
            }

            List<MethodLocation> placed = new ArrayList<>();

            for (int i = 0; i < instructions.size(); i++) {
                BuilderInstruction placeholder =
                        new BuilderInstruction10x(org.jf.dexlib2.Opcode.NOP);

                if (i == 0 && delete > 0) {
                    mutable.replaceInstruction(index, placeholder);
                } else {
                    mutable.addInstruction(index + i, placeholder);
                }

                placed.add(mutable.getInstructions().get(index + i).getLocation());
            }

            // The instruction after the edit; none when the edit ends the method.
            int after = index + instructions.size();
            MethodLocation next =
                    after < mutable.getInstructions().size()
                            ? mutable.getInstructions().get(after).getLocation()
                            : null;

            for (var number : insertion.lineNumbers().entrySet()) {
                at(placed, next, number.getKey()).addLineNumber(number.getValue());
            }

            if (change.moveLabels() && !instructions.isEmpty()) {
                List<Label> moved = new ArrayList<>(next.getLabels());
                next.getLabels().clear();
                placed.get(0).getLabels().addAll(moved);
            }

            for (var label : insertion.labels().entrySet()) {
                Failure.require(
                        declared.put(label.getKey(), at(placed, next, label.getValue())) == null,
                        "DEX_EDIT_INVALID",
                        "Duplicate label: " + label.getKey(),
                        5);
            }

            return new Inserted(instructions, placed, insertion.catches());
        }

        private MethodLocation at(List<MethodLocation> placed, MethodLocation next, int position) {
            MethodLocation location = position < placed.size() ? placed.get(position) : next;

            Failure.require(
                    location != null,
                    "DEX_EDIT_INVALID",
                    "Label or line after the last instruction of " + method,
                    5);

            return location;
        }

        /** A label declared by inserted code or the original label at a code address. */
        private Label resolve(String name) {
            MethodLocation location = declared.get(name);

            if (location == null && name.matches(":(?:cond|goto)_[0-9a-f]+")) {
                location = existing.get(name.substring(name.lastIndexOf('_') + 1));
            }

            Failure.require(
                    location != null
                            && location.getIndex() < mutable.getInstructions().size()
                            && mutable.getInstructions()
                                    .get(location.getIndex())
                                    .getLocation()
                                    .equals(location),
                    "DEX_EDIT_INVALID",
                    "Unknown label " + name + " in " + method,
                    5);

            return labels.computeIfAbsent(
                    location, key -> mutable.newLabelForIndex(key.getIndex()));
        }
    }

    private static int match(List<String> lines, List<String> anchor, String method) {
        int found = -1, matches = 0;

        for (int i = 0; i + anchor.size() <= lines.size(); i++) {
            if (lines.subList(i, i + anchor.size()).equals(anchor)) {
                found = i;
                matches++;
            }
        }

        Failure.require(
                matches == 1,
                "DEX_EDIT_ANCHOR",
                (matches == 0 ? "Anchor absent in " : "Ambiguous anchor in ")
                        + method
                        + ": "
                        + anchor.get(0),
                5);

        return found;
    }

    /** Moves the parameter registers to the end of a frame with count registers. */
    static MethodImplementation registers(MethodImplementation code, int count, int parameters) {
        int before = code.getRegisterCount();

        if (count == before) {
            return code;
        }

        Failure.require(
                count >= parameters,
                "DEX_EDIT_INVALID",
                "Register count below parameter registers",
                5);
        int first = before - parameters, shift = count - before;
        IntUnaryOperator map = register -> register >= first ? register + shift : register;
        List<Instruction> instructions = new ArrayList<>();

        for (Instruction instruction : code.getInstructions()) {
            try {
                instructions.add(registers(instruction, map));
            } catch (IllegalArgumentException error) {
                // A moved parameter register no longer fits the register field of the format.
                throw new Failure(
                        "DEX_EDIT_INVALID",
                        "Register change exceeds " + instruction.getOpcode().name,
                        5);
            }
        }

        List<DebugItem> debug = new ArrayList<>();

        for (DebugItem item : code.getDebugItems()) {
            if (item instanceof StartLocal local) {
                debug.add(
                        new ImmutableStartLocal(
                                item.getCodeAddress(),
                                map.applyAsInt(local.getRegister()),
                                local.getName(),
                                local.getType(),
                                local.getSignature()));
            } else if (item instanceof EndLocal local) {
                debug.add(
                        new ImmutableEndLocal(
                                item.getCodeAddress(),
                                map.applyAsInt(local.getRegister()),
                                local.getName(),
                                local.getType(),
                                local.getSignature()));
            } else if (item instanceof RestartLocal local) {
                debug.add(
                        new ImmutableRestartLocal(
                                item.getCodeAddress(),
                                map.applyAsInt(local.getRegister()),
                                local.getName(),
                                local.getType(),
                                local.getSignature()));
            } else {
                debug.add(ImmutableDebugItem.of(item));
            }
        }

        return new ImmutableMethodImplementation(count, instructions, code.getTryBlocks(), debug);
    }

    private static Instruction registers(Instruction instruction, IntUnaryOperator map) {
        var opcode = instruction.getOpcode();
        Format format = opcode.format;
        Instruction result =
                switch (format) {
                    case Format11n ->
                            new ImmutableInstruction11n(
                                    opcode, a(instruction, map), literal(instruction));
                    case Format11x -> new ImmutableInstruction11x(opcode, a(instruction, map));
                    case Format12x ->
                            new ImmutableInstruction12x(
                                    opcode, a(instruction, map), b(instruction, map));
                    case Format21c ->
                            new ImmutableInstruction21c(
                                    opcode, a(instruction, map), reference(instruction));
                    case Format21ih ->
                            new ImmutableInstruction21ih(
                                    opcode, a(instruction, map), literal(instruction));
                    case Format21lh ->
                            new ImmutableInstruction21lh(
                                    opcode,
                                    a(instruction, map),
                                    ((WideLiteralInstruction) instruction).getWideLiteral());
                    case Format21s ->
                            new ImmutableInstruction21s(
                                    opcode, a(instruction, map), literal(instruction));
                    case Format21t ->
                            new ImmutableInstruction21t(
                                    opcode, a(instruction, map), offset(instruction));
                    case Format22b ->
                            new ImmutableInstruction22b(
                                    opcode,
                                    a(instruction, map),
                                    b(instruction, map),
                                    literal(instruction));
                    case Format22c ->
                            new ImmutableInstruction22c(
                                    opcode,
                                    a(instruction, map),
                                    b(instruction, map),
                                    reference(instruction));
                    case Format22s ->
                            new ImmutableInstruction22s(
                                    opcode,
                                    a(instruction, map),
                                    b(instruction, map),
                                    literal(instruction));
                    case Format22t ->
                            new ImmutableInstruction22t(
                                    opcode,
                                    a(instruction, map),
                                    b(instruction, map),
                                    offset(instruction));
                    case Format22x ->
                            new ImmutableInstruction22x(
                                    opcode, a(instruction, map), b(instruction, map));
                    case Format23x ->
                            new ImmutableInstruction23x(
                                    opcode,
                                    a(instruction, map),
                                    b(instruction, map),
                                    map.applyAsInt(
                                            ((ThreeRegisterInstruction) instruction)
                                                    .getRegisterC()));
                    case Format31c ->
                            new ImmutableInstruction31c(
                                    opcode, a(instruction, map), reference(instruction));
                    case Format31i ->
                            new ImmutableInstruction31i(
                                    opcode, a(instruction, map), literal(instruction));
                    case Format31t ->
                            new ImmutableInstruction31t(
                                    opcode, a(instruction, map), offset(instruction));
                    case Format32x ->
                            new ImmutableInstruction32x(
                                    opcode, a(instruction, map), b(instruction, map));
                    case Format51l ->
                            new ImmutableInstruction51l(
                                    opcode,
                                    a(instruction, map),
                                    ((WideLiteralInstruction) instruction).getWideLiteral());
                    case Format35c -> {
                        var five = (FiveRegisterInstruction) instruction;
                        yield new ImmutableInstruction35c(
                                opcode,
                                five.getRegisterCount(),
                                map.applyAsInt(five.getRegisterC()),
                                map.applyAsInt(five.getRegisterD()),
                                map.applyAsInt(five.getRegisterE()),
                                map.applyAsInt(five.getRegisterF()),
                                map.applyAsInt(five.getRegisterG()),
                                reference(instruction));
                    }
                    case Format3rc -> {
                        var range = (RegisterRangeInstruction) instruction;
                        int start = map.applyAsInt(range.getStartRegister());
                        Failure.require(
                                range.getRegisterCount() == 0
                                        || map.applyAsInt(
                                                        range.getStartRegister()
                                                                + range.getRegisterCount()
                                                                - 1)
                                                == start + range.getRegisterCount() - 1,
                                "DEX_EDIT_INVALID",
                                "Register range spans locals and parameters",
                                5);
                        yield new ImmutableInstruction3rc(
                                opcode, start, range.getRegisterCount(), reference(instruction));
                    }
                    case Format10x,
                            Format10t,
                            Format20t,
                            Format30t,
                            ArrayPayload,
                            PackedSwitchPayload,
                            SparseSwitchPayload ->
                            ImmutableInstruction.of(instruction);
                    default ->
                            throw new Failure(
                                    "DEX_EDIT_UNSUPPORTED",
                                    "Unsupported instruction for a register change: " + opcode.name,
                                    5);
                };

        return result;
    }

    private static int a(Instruction instruction, IntUnaryOperator map) {
        return map.applyAsInt(((OneRegisterInstruction) instruction).getRegisterA());
    }

    private static int b(Instruction instruction, IntUnaryOperator map) {
        return map.applyAsInt(((TwoRegisterInstruction) instruction).getRegisterB());
    }

    private static int literal(Instruction instruction) {
        return ((NarrowLiteralInstruction) instruction).getNarrowLiteral();
    }

    private static int offset(Instruction instruction) {
        return ((OffsetInstruction) instruction).getCodeOffset();
    }

    private static org.jf.dexlib2.iface.reference.Reference reference(Instruction instruction) {
        return ((ReferenceInstruction) instruction).getReference();
    }
}
