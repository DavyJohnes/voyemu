package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MethodImplementationBuilder;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11n;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodParameter;
import org.jf.dexlib2.immutable.reference.ImmutableFieldReference;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/** Method-level DEX edits: anchors, label placement, register frames, catches, DEX rewrite. */
public final class DexEditsTest {
    private static final String TYPE = "Lcom/voyemu/fixture/Target;";
    private static final String METHOD = TYPE + "->run(I)V";
    private static final String LOG = "invoke-virtual {p0}, " + TYPE + "->log()V";

    interface Action {
        void run() throws Exception;
    }

    static void check(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    static void rejected(Action action, String code) throws Exception {
        try {
            action.run();
        } catch (Failure failure) {
            check(failure.code.equals(code), "expected " + code + ", got " + failure.code);

            return;
        }

        throw new AssertionError("expected " + code);
    }

    /**
     * Instance method run(I)V with 4 registers (v0, v1, p0, p1): if-eqz p1 to the field write, a
     * log call twice, the field write, return.
     */
    static ClassDef fixture() {
        var builder = new MethodImplementationBuilder(4);
        var field = new ImmutableFieldReference(TYPE, "value", "I");
        var call = new ImmutableMethodReference(TYPE, "log", List.of(), "V");

        builder.addInstruction(
                new BuilderInstruction21t(Opcode.IF_EQZ, 3, builder.getLabel("set")));
        builder.addInstruction(
                new BuilderInstruction35c(Opcode.INVOKE_VIRTUAL, 1, 2, 0, 0, 0, 0, call));
        builder.addInstruction(
                new BuilderInstruction35c(Opcode.INVOKE_VIRTUAL, 1, 2, 0, 0, 0, 0, call));
        builder.addLabel("set");
        builder.addInstruction(new BuilderInstruction11n(Opcode.CONST_4, 0, 1));
        builder.addInstruction(new BuilderInstruction22c(Opcode.IPUT, 0, 2, field));
        builder.addInstruction(new BuilderInstruction10x(Opcode.RETURN_VOID));
        var run =
                new ImmutableMethod(
                        TYPE,
                        "run",
                        List.of(new ImmutableMethodParameter("I", null, null)),
                        "V",
                        1,
                        null,
                        null,
                        builder.getMethodImplementation());
        var logBuilder = new MethodImplementationBuilder(1);
        logBuilder.addInstruction(new BuilderInstruction10x(Opcode.RETURN_VOID));
        var logMethod =
                new ImmutableMethod(
                        TYPE,
                        "log",
                        List.of(),
                        "V",
                        1,
                        null,
                        null,
                        logBuilder.getMethodImplementation());

        return new ImmutableClassDef(
                TYPE,
                1,
                "Ljava/lang/Object;",
                null,
                null,
                null,
                List.of(
                        new org.jf.dexlib2.immutable.ImmutableField(
                                TYPE, "value", "I", 2, null, null, null)),
                List.of(run, logMethod));
    }

    static Method run(ClassDef type) {
        for (Method method : type.getMethods()) {
            if (DexEdits.descriptor(method).equals(METHOD)) {
                return method;
            }
        }

        throw new AssertionError("run missing");
    }

    static List<String> lines(Method method) {
        var code = method.getImplementation();

        return DexEdits.render(
                code, new DexText.Frame(code.getRegisterCount(), DexEdits.parameters(method)));
    }

    static ClassDef edit(ClassDef type, Integer registers, DexEdits.Edit... edits) {
        return DexEdits.apply(
                type, List.of(new DexEdits.MethodEdit(METHOD, registers, List.of(edits))));
    }

    public static void main(String[] args) throws Exception {
        ClassDef original = fixture();

        checkRendering(original);
        checkReplace(original);
        checkAnchors(original);
        checkRemovedTarget(original);
        checkLabels(original);
        checkRegisters(original);
        checkCatch(original);
        checkJson(original);
        checkClasses(original);
        System.out.println(
                "Method edits: anchors, label placement, register frames, catches and DEX rewrite"
                        + " passed");
    }

    static void checkRendering(ClassDef original) throws Exception {

        check(
                lines(run(original))
                        .equals(
                                List.of(
                                        "if-eqz p1, :cond_8",
                                        LOG,
                                        LOG,
                                        "const/4 v0, 0x1",
                                        "iput v0, p0, " + TYPE + "->value:I",
                                        "return-void")),
                "smali rendering of the fixture: " + lines(run(original)));
    }

    static void checkReplace(ClassDef original) throws Exception {
        // A replacement keeps the branch target on its first inserted instruction.
        ClassDef replaced =
                edit(
                        original,
                        null,
                        DexEdits.Edit.replace(
                                List.of("const/4 v0, 0x1", "iput v0, p0, " + TYPE + "->value:I"),
                                List.of("const/4 v1, 0x2", "iput v1, p0, " + TYPE + "->value:I")));

        check(
                lines(run(replaced))
                        .equals(
                                List.of(
                                        "if-eqz p1, :cond_8",
                                        LOG,
                                        LOG,
                                        "const/4 v1, 0x2",
                                        "iput v1, p0, " + TYPE + "->value:I",
                                        "return-void")),
                "replacement: " + lines(run(replaced)));
    }

    static void checkAnchors(ClassDef original) throws Exception {
        // Anchors must match exactly once and edits may not overlap.
        rejected(
                () -> edit(original, null, DexEdits.Edit.replace(List.of(LOG), List.of())),
                "DEX_EDIT_ANCHOR");
        rejected(
                () ->
                        edit(
                                original,
                                null,
                                DexEdits.Edit.replace(List.of("const/4 v0, 0x2"), List.of())),
                "DEX_EDIT_ANCHOR");
        rejected(
                () ->
                        edit(
                                original,
                                null,
                                DexEdits.Edit.replace(List.of("return-void"), List.of("nop")),
                                DexEdits.Edit.replace(
                                        List.of(
                                                "iput v0, p0, " + TYPE + "->value:I",
                                                "return-void"),
                                        List.of("nop"))),
                "DEX_EDIT_OVERLAP");
    }

    static void checkRemovedTarget(ClassDef original) throws Exception {
        // Removing an instruction that a branch targets, other than the first, is refused.
        rejected(
                () ->
                        edit(
                                original,
                                null,
                                DexEdits.Edit.replace(
                                        List.of(LOG, LOG, "const/4 v0, 0x1"), List.of("nop"))),
                "DEX_EDIT_ANCHOR");
    }

    static void checkLabels(ClassDef original) throws Exception {
        // Insertion before a branch target takes its labels; insertion after keeps them.
        ClassDef before =
                edit(
                        original,
                        null,
                        DexEdits.Edit.before(List.of("const/4 v0, 0x1"), List.of("nop")));

        check(
                lines(run(before)).get(0).equals("if-eqz p1, :cond_8")
                        && lines(run(before)).get(3).equals("nop"),
                "before: branch reaches inserted code " + lines(run(before)));
        ClassDef after =
                edit(original, null, DexEdits.Edit.after(List.of(LOG, LOG), List.of("nop")));
        check(
                lines(run(after)).get(0).equals("if-eqz p1, :cond_9")
                        && lines(run(after)).get(3).equals("nop"),
                "after: branch skips inserted code " + lines(run(after)));
    }

    static void checkRegisters(ClassDef original) throws Exception {
        // A larger frame moves the parameters; p names keep their meaning.
        ClassDef wider =
                edit(
                        original,
                        6,
                        DexEdits.Edit.replace(
                                List.of("const/4 v0, 0x1"),
                                List.of("const/4 v0, 0x1", "move v3, p1")));
        Method widened = run(wider);

        check(
                widened.getImplementation().getRegisterCount() == 6
                        && lines(widened)
                                .equals(
                                        List.of(
                                                "if-eqz p1, :cond_8",
                                                LOG,
                                                LOG,
                                                "const/4 v0, 0x1",
                                                "move v3, p1",
                                                "iput v0, p0, " + TYPE + "->value:I",
                                                "return-void")),
                "register frame: " + lines(widened));
        var first = widened.getImplementation().getInstructions().iterator().next();
        check(
                ((org.jf.dexlib2.iface.instruction.OneRegisterInstruction) first).getRegisterA()
                        == 5,
                "p1 is v5 in a frame of 6");
    }

    static void checkCatch(ClassDef original) throws Exception {
        // An inserted try block covers the guarded call; its handler is the next instruction.
        ClassDef guarded =
                edit(
                        original,
                        null,
                        DexEdits.Edit.replace(
                                List.of(LOG, "const/4 v0, 0x1"),
                                List.of(
                                        ":try_start_guard",
                                        LOG,
                                        ":try_end_guard",
                                        ".catch Ljava/lang/NoSuchMethodError; {:try_start_guard"
                                                + " .. :try_end_guard} :catch_guard",
                                        ":catch_guard",
                                        "const/4 v0, 0x1")));
        var tries = run(guarded).getImplementation().getTryBlocks();

        check(tries.size() == 1, "one try block");
        var block = tries.get(0);
        check(
                block.getStartCodeAddress() == 5
                        && block.getCodeUnitCount() == 3
                        && block.getExceptionHandlers().size() == 1
                        && block.getExceptionHandlers()
                                .get(0)
                                .getExceptionType()
                                .equals("Ljava/lang/NoSuchMethodError;")
                        && block.getExceptionHandlers().get(0).getHandlerCodeAddress() == 8,
                "try block covers the second call, handler at the field write");
        rejected(
                () ->
                        edit(
                                original,
                                null,
                                DexEdits.Edit.before(
                                        List.of("return-void"), List.of("goto :missing"))),
                "DEX_EDIT_INVALID");
    }

    static void checkJson(ClassDef original) throws Exception {
        // The JSON form selects the operation by its fields.
        var parsed =
                DexEdits.MethodEdit.parse(
                        List.of(
                                METHOD,
                                Map.of(
                                        "registers",
                                        5,
                                        "edits",
                                        List.of(
                                                Map.of(
                                                        "after",
                                                        List.of(LOG, LOG),
                                                        "insert",
                                                        List.of("nop"))))));

        check(parsed.registers() == 5 && parsed.edits().get(0).offset() == 2, "parsed after-edit");
        rejected(
                () -> DexEdits.MethodEdit.parse(List.of(METHOD, Map.of("lines", List.of()))),
                "DEX_EDIT_INVALID");
        rejected(
                () ->
                        DexEdits.MethodEdit.parse(
                                List.of(
                                        METHOD,
                                        Map.of(
                                                "edits",
                                                List.of(
                                                        Map.of(
                                                                "replace",
                                                                List.of("  nop"),
                                                                "with",
                                                                List.of()))))),
                "DEX_EDIT_INVALID");
    }

    static void checkClasses(ClassDef original) throws Exception {
        // DexClasses rewrites only the edited DEX entry and keeps its class inventory.
        byte[] dex =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(28), List.of(original)));
        Map<String, byte[]> archive = new TreeMap<>(Map.of("classes.dex", dex));
        var classes = new DexClasses(archive, List.of("classes.dex"), 28);

        classes.edit(List.of(DexEdits.MethodEdit.parse(List.of(METHOD, Map.of("registers", 5)))));
        classes.write(archive);
        var rewritten = new DexBackedDexFile(Opcodes.forApi(28), archive.get("classes.dex"));
        check(
                rewritten.getClasses().size() == 1
                        && run(rewritten.getClasses().iterator().next())
                                        .getImplementation()
                                        .getRegisterCount()
                                == 5,
                "rewritten DEX entry");
        rejected(() -> classes.get("Lcom/voyemu/fixture/Absent;"), "DEX_EDIT_ANCHOR");
    }
}
