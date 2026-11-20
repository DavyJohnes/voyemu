package com.voyemu.prepare;

import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.zip.ZipFile;

public final class HiboardStartupGuardTest {
    static void check(boolean b, String s) {
        if (!b) {
            throw new AssertionError(s);
        }
    }

    interface Action {
        void run() throws Exception;
    }

    static void reject(Action a) throws Exception {
        try {
            a.run();
            throw new AssertionError("invalid accepted");
        } catch (Failure e) {
            check(e.code.equals("HIBOARD_STARTUP"), e.code);
        }
    }

    record Light(int fog) {}

    static class State {
        int state = 77, highlight = 1, theme;
        List<Integer> images = new ArrayList<>();
    }

    // Execute actual OEM rear-fog bytecode, including conditional control flow and common theme
    // tail.
    // Android view/log/string calls have narrow inert stand-ins; unknown opcodes fail closed.
    static State run(Method m, Light light, State state) {
        var xs = new TreeMap<Integer, Instruction>();
        int at = 0;

        for (var x : m.getImplementation().getInstructions()) {
            xs.put(at, x);
            at += x.getCodeUnits();
        }

        Object[] r = new Object[14];
        r[9] = 1;
        r[10] = 0;
        r[12] = state;
        Object result = null;
        int pc = 0x209;

        for (int steps = 0; steps < 250; steps++) {
            var x = xs.get(pc);
            check(x != null, "bad pc " + pc);
            int next = pc + x.getCodeUnits();
            String ref = x instanceof ReferenceInstruction ri ? ri.getReference().toString() : "";
            int a = x instanceof OneRegisterInstruction q ? q.getRegisterA() : -1;
            int b = x instanceof TwoRegisterInstruction q ? q.getRegisterB() : -1;

            switch (x.getOpcode()) {
                case INVOKE_STATIC, INVOKE_VIRTUAL, INVOKE_DIRECT -> {
                    var q = (FiveRegisterInstruction) x;
                    Object receiver = r[q.getRegisterC()];
                    result = null;

                    if (ref.endsWith("->getCanBusManager()Lcom/qinggan/canbus/CanBusManager;")) {
                        result = new Object();
                    } else if (ref.contains("->getLightStatus()")) {
                        result = light;
                    } else if (ref.contains("->getFogRear()")) {
                        result = ((Light) receiver).fog();
                    } else if (ref.contains("->setImageLevel(I)")) {
                        state.images.add((Integer) r[q.getRegisterD()]);
                    } else if (ref.endsWith("->updateTextColor()V")) {
                        state.theme++;
                    } else if (ref.contains("->append(")) {
                        result = receiver;
                    } else if (ref.contains("->toString()")) {
                        result = "log";
                    } else if (ref.contains("->getLayoutParams()")) {
                        result = null;
                    } else {
                        check(
                                ref.contains("StringBuilder;-><init>")
                                        || ref.startsWith("Landroid/util/Log;"),
                                "unsupported call " + ref);
                    }
                }
                case MOVE_RESULT_OBJECT, MOVE_RESULT -> r[a] = result;
                case IPUT, IPUT_BOOLEAN -> {
                    if (ref.endsWith("->mState:I")) {
                        state.state = (Integer) r[a];
                    } else if (ref.endsWith("->highLight:Z")) {
                        state.highlight = (Integer) r[a];
                    } else {
                        throw new AssertionError(ref);
                    }
                }
                case IGET, IGET_BOOLEAN -> {
                    if (ref.endsWith("->mState:I")) {
                        r[a] = state.state;
                    } else if (ref.endsWith("->highLight:Z")) {
                        r[a] = state.highlight;
                    } else {
                        throw new AssertionError(ref);
                    }
                }
                case IGET_OBJECT, SGET_OBJECT, NEW_INSTANCE -> r[a] = new Object();
                case CONST_STRING, CONST_STRING_JUMBO -> r[a] = "text";
                case CONST_4, CONST_16, CONST ->
                        r[a] = ((NarrowLiteralInstruction) x).getNarrowLiteral();
                case MOVE -> r[a] = r[b];
                case IF_EQZ -> {
                    if (r[a] == null || (r[a] instanceof Integer n && n == 0)) {
                        next = pc + ((OffsetInstruction) x).getCodeOffset();
                    }
                }
                case IF_NEZ -> {
                    if (r[a] != null && (!(r[a] instanceof Integer n) || n != 0)) {
                        next = pc + ((OffsetInstruction) x).getCodeOffset();
                    }
                }
                case GOTO, GOTO_16, GOTO_32 -> next = pc + ((OffsetInstruction) x).getCodeOffset();
                case INSTANCE_OF -> r[a] = 0; // null layout params takes existing common tail
                case RETURN_VOID -> {
                    return state;
                }
                default ->
                        throw new AssertionError(
                                "unsupported " + Integer.toHexString(pc) + " " + x.getOpcode());
            }

            pc = next;
        }

        throw new AssertionError("loop");
    }

    public static void main(String[] a) throws Exception {
        Path original = Path.of(a[0]), job = Path.of(a[1]);
        Map<String, Object> constants =
                TestProfiles.constants(
                        Path.of("").toAbsolutePath(), "api30-hiboard-startup", original);
        byte[] clean;

        try (var z = new ZipFile(original.toFile())) {
            clean = z.getInputStream(z.getEntry("classes.dex")).readAllBytes();
        }

        byte[] pre = HiboardRouting.apply(constants, 30, original, clean);
        var before = HiboardStartupGuard.classes(pre);
        var old = HiboardStartupGuard.target(before);
        boolean red = false;

        try {
            run(old, null, new State());
        } catch (NullPointerException expected) {
            red = true;
        }

        check(red, "original must fail on missing light snapshot");
        byte[] out = HiboardStartupGuard.apply(constants, 30, original, pre);
        var after = HiboardStartupGuard.classes(out);
        var now = HiboardStartupGuard.target(after);
        var unavailable = run(now, null, new State());
        check(
                unavailable.state == 77
                        && unavailable.highlight == 1
                        && unavailable.images.isEmpty()
                        && unavailable.theme == 1,
                "null preserves known state and runs theme tail");

        for (int value : new int[] {0, 1}) {
            State was = run(old, new Light(value), new State()),
                    is = run(now, new Light(value), new State());
            check(
                    was.state == value
                            && was.highlight == value
                            && was.images.equals(List.of(value, value))
                            && was.theme == 1,
                    "OEM expected values " + value);
            check(
                    was.state == is.state
                            && was.highlight == is.highlight
                            && was.images.equals(is.images)
                            && is.theme == 1,
                    "nonnull OEM behavior preserved");
        }

        var replay = run(now, new Light(1), unavailable);
        check(
                replay.state == 1
                        && replay.highlight == 1
                        && replay.theme == 2
                        && replay.images.equals(List.of(1, 1)),
                "real snapshot after null updates normally");
        int changes = 0;

        for (var e : before.entrySet()) {
            if (!FrameworkDexMaterializer.semantic(e.getValue())
                    .equals(FrameworkDexMaterializer.semantic(after.get(e.getKey())))) {
                check(e.getKey().equals(HiboardStartupGuard.OWNER), "foreign class modified");
                changes++;
                var om = new TreeMap<String, String>();

                for (var m : e.getValue().getMethods()) {
                    om.put(m.toString(), FrameworkDexMaterializer.semantic(m));
                }

                for (var m : after.get(e.getKey()).getMethods()) {
                    if (!m.toString().equals(HiboardStartupGuard.TARGET)) {
                        check(
                                om.get(m.toString()).equals(FrameworkDexMaterializer.semantic(m)),
                                "foreign method modified");
                    }
                }
            }
        }

        check(changes == 1, "one changed class");
        reject(() -> HiboardStartupGuard.apply(constants, 28, original, pre));
        reject(() -> HiboardStartupGuard.apply(constants, 30, original, clean));
        reject(() -> HiboardStartupGuard.apply(constants, 30, original, out));
        reject(() -> HiboardStartupGuard.validate(now));
        Files.write(job.resolve("classes.dex"), out);
        Files.writeString(
                job.resolve("receipt.txt"),
                "original_null=RED_NPE\n"
                        + "patched_null=preserve_state_77_highlight_1_no_images_theme_1\n"
                        + "nonnull_0_1=OEM_identical\n"
                        + "after_null_real_1=applied\n"
                        + "changed_classes=1\n"
                        + "negative_guards=6\n"
                        + "predecessor_sha256="
                        + NativeCore.hash(pre)
                        + "\noutput_sha256="
                        + NativeCore.hash(out)
                        + "\nART=not_run\n");
        System.out.println(Files.readString(job.resolve("receipt.txt")));
    }
}
