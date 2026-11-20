package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;
import org.jf.dexlib2.iface.reference.FieldReference;
import org.jf.dexlib2.iface.reference.StringReference;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.SortedMap;
import java.util.TreeMap;
import java.util.zip.ZipFile;

public class SettingsVoiceCloneGuardTest {
    static final Map<String, Object>
            STEP = step("6f96bbeb58bf915b0a2d60817082cdca3523e3a2d00c144544cb92b4e5be6870"),
            FOREIGN = step("0".repeat(64));

    static Map<String, Object> step(String pin) {
        return Map.of(
                StepConstants.PINS,
                List.of(pin),
                "settings_voice_clone_boundary",
                false,
                "sound_balance",
                true);
    }

    static void check(boolean b, String s) {
        if (!b) {
            throw new AssertionError(s);
        }
    }

    static final String O = SettingsVoiceCloneGuard.OWNER;

    static Map<String, ClassDef> classes(byte[] b) {
        var a = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), b).getClasses()) {
            a.put(c.getType(), c);
        }

        return a;
    }

    static Method method(byte[] b, String n) {
        for (var m : classes(b).get(O).getMethods()) {
            if (m.getName().equals(n)) {
                return m;
            }
        }

        throw new AssertionError(n);
    }

    static class State {
        boolean provider, activity = true;
        int helper, observer, cursor;
        Map<String, Integer> view = new TreeMap<>();
        List<String> events = new ArrayList<>();
        Object clone;

        State(boolean p) {
            provider = p;

            for (var s : List.of("radioMyVoice", "myVoiceTips", "voiceSelectRecordVoice")) {
                view.put(s, 0);
            }
        }
    }

    static boolean zero(Object o) {
        return o == null || (o instanceof Integer n && n == 0);
    }

    // Execute the actual pinned OEM/transformed DEX. External Android calls are strict stand-ins;
    // helper init reproduces the captured provider registration failure and real cursor boundary.
    static void run(Method m, State s, String argument) {
        var xs = byAddress(m);
        Object[] r = new Object[m.getImplementation().getRegisterCount()];
        int self = r.length - 1 - m.getParameterTypes().size();

        r[self] = s;

        if (argument != null) {
            r[self + 1] = argument;
        }

        Object result = null;
        int pc = 0;

        for (int step = 0; step < 1000; step++) {
            var x = xs.get(pc);
            check(x != null, "badpc " + pc);
            int next = pc + x.getCodeUnits();
            String ref = x instanceof ReferenceInstruction q ? q.getReference().toString() : "";
            int a = x instanceof OneRegisterInstruction q ? q.getRegisterA() : -1,
                    b = x instanceof TwoRegisterInstruction q ? q.getRegisterB() : -1;

            switch (x.getOpcode()) {
                case INVOKE_STATIC, INVOKE_VIRTUAL, INVOKE_DIRECT, INVOKE_INTERFACE, INVOKE_SUPER ->
                        result = invoke(ref, (FiveRegisterInstruction) x, r, s);
                case IGET_OBJECT -> r[a] = field(ref, x, s);
                case IPUT_OBJECT -> {
                    if (ref.endsWith(
                            "->voiceCloneHelper:Lcom/qinggan/app/setting/utils/VoiceCloneHelper;")) {
                        s.clone = zero(r[a]) ? null : r[a];
                    }
                }
                case SGET_OBJECT, NEW_INSTANCE -> r[a] = "object";
                case CONST_STRING, CONST_STRING_JUMBO ->
                        r[a] =
                                ((StringReference) ((ReferenceInstruction) x).getReference())
                                        .getString();
                case CONST_4, CONST_16, CONST ->
                        r[a] = ((NarrowLiteralInstruction) x).getNarrowLiteral();
                case MOVE_RESULT, MOVE_RESULT_OBJECT -> r[a] = result;
                case MOVE, MOVE_OBJECT, MOVE_FROM16, MOVE_OBJECT_FROM16 -> r[a] = r[b];
                case CHECK_CAST, NOP -> {}
                case IF_EQZ, IF_NEZ, IF_EQ, IF_NE -> next = branch(x, r, a, b, pc, next);
                case GOTO, GOTO_16, GOTO_32 -> next = pc + ((OffsetInstruction) x).getCodeOffset();
                case RETURN_VOID -> {
                    return;
                }
                default ->
                        throw new AssertionError(
                                "unknown opcode " + Integer.toHexString(pc) + " " + x.getOpcode());
            }

            pc = next;
        }

        throw new AssertionError("loop");
    }

    static SortedMap<Integer, Instruction> byAddress(Method m) {
        var xs = new TreeMap<Integer, Instruction>();
        int z = 0;

        for (var x : m.getImplementation().getInstructions()) {
            xs.put(z, x);
            z += x.getCodeUnits();
        }

        return xs;
    }

    static int branch(Instruction x, Object[] r, int a, int b, int pc, int next) {
        boolean taken =
                switch (x.getOpcode()) {
                    case IF_EQZ -> zero(r[a]);
                    case IF_NEZ -> !zero(r[a]);
                    case IF_EQ -> Objects.equals(r[a], r[b]);
                    default -> !Objects.equals(r[a], r[b]);
                };

        return taken ? pc + ((OffsetInstruction) x).getCodeOffset() : next;
    }

    static Object field(String ref, Instruction x, State s) {
        if (ref.endsWith("->voiceCloneHelper:Lcom/qinggan/app/setting/utils/VoiceCloneHelper;")) {
            return s.clone;
        } else if (ref.endsWith("->voiceCloneRadioButtons:Ljava/util/List;")) {
            return List.of();
        } else if (ref.contains("FragmentVoiceBinding;->")) {
            return ((FieldReference) ((ReferenceInstruction) x).getReference()).getName();
        } else {
            return "object";
        }
    }

    static Object invoke(String ref, FiveRegisterInstruction q, Object[] r, State s) {
        Object receiver = r[q.getRegisterC()], arg = r[q.getRegisterD()];
        Object result = null;

        if (ref.contains("->getActivity()")) {
            result = s.activity ? "activity" : null;
        } else if (ref.contains("->getContext()")
                || ref.contains("->getApplicationContext()")
                || ref.contains("->getInstance()")
                || ref.contains("EventBus;->getDefault()")) {
            result = "context";
        } else if (ref.contains("->getPackageManager()")) {
            result = "packageManager";
        } else if (ref.contains("->resolveContentProvider(")) {
            check(SettingsVoiceCloneGuard.AUTH.equals(arg), "authority");
            check(Integer.valueOf(0).equals(r[q.getRegisterE()]), "flags");
            result = s.provider ? "provider" : null;
        } else if (ref.contains("->getCurrentUserId()")) {
            result = "fixture-user";
        } else if (ref.contains("VoiceCloneHelper;-><init>")) {
            s.helper++;
            s.events.add("helper");
        } else if (ref.contains("VoiceCloneHelper;->initVoiceClone()")) {
            s.observer++;
            s.events.add("observer");

            if (!s.provider) {
                throw new SecurityException("missing com.qinggan.qingai.clone.voice");
            }

            s.cursor++;
            s.events.add("cursor");
            result = List.of();
        } else {
            result = invokeView(ref, receiver, arg, s);
        }

        return result;
    }

    static Object invokeView(String ref, Object receiver, Object arg, State s) {
        Object result = null;

        if (ref.contains("->modifyVoiceRadioGroup(")) {
            s.events.add("modifyVoiceRadioGroup");
        } else if (ref.contains("->setVisibility(I)")) {
            s.view.put((String) receiver, (Integer) arg);
            s.events.add("visibility:" + receiver + ":" + arg);
        } else if (ref.contains("->setSliderWidth(I)")) {
            s.events.add("slider:" + arg);
        } else if (ref.contains("EventBus;->isRegistered(")) {
            result = 1;
        } else if (ref.contains("->hashCode()")) {
            result = receiver.hashCode();
        } else if (ref.contains("->equals(Ljava/lang/Object;)")) {
            result = receiver.equals(arg) ? 1 : 0;
        } else if (ref.contains("->iterator()")) {
            result = List.of().iterator();
        } else if (ref.contains("->hasNext()")) {
            result = ((Iterator<?>) receiver).hasNext() ? 1 : 0;
        } else if (ref.contains("->append(")) {
            result = receiver;
        } else if (ref.contains("->toString()")) {
            result = "log";
        } else if (ref.contains("->closeVoiceCloneObserve()")) {
            check(receiver != null, "nullhelper");
            s.events.add("close");
        } else if (ref.contains("->hideDialog()")) {
            s.events.add("hideDialog");
        } else if (ref.contains("->onDestroy()")) {
            s.events.add("onDestroy");
        } else {
            check(
                    ref.contains("-><init>")
                            || ref.startsWith("Landroid/util/Log;")
                            || ref.endsWith("->onPause()V")
                            || ref.contains("->remove")
                            || ref.contains("EventBus;->unregister("),
                    "unknown call " + ref);
        }

        return result;
    }

    static void hidden(State s) {
        for (var n : List.of("radioMyVoice", "myVoiceTips", "voiceSelectRecordVoice")) {
            check(s.view.get(n) == 8, "dead clone visible " + n);
        }
    }

    static Method stripped(Method m) {
        var b = m.getImplementation();

        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                b == null
                        ? null
                        : new ImmutableMethodImplementation(
                                b.getRegisterCount(),
                                b.getInstructions(),
                                List.of(),
                                b.getDebugItems()));
    }

    static ClassDef stripped(ClassDef c) {
        var d = new ArrayList<Method>();
        var v = new ArrayList<Method>();

        for (var m : c.getDirectMethods()) {
            d.add(stripped(m));
        }

        for (var m : c.getVirtualMethods()) {
            v.add(stripped(m));
        }

        return new ImmutableClassDef(
                c.getType(),
                c.getAccessFlags(),
                c.getSuperclass(),
                c.getInterfaces(),
                c.getSourceFile(),
                c.getAnnotations(),
                c.getStaticFields(),
                c.getInstanceFields(),
                d,
                v);
    }

    static Map<Integer, String> coverage(Method m) {
        var out = new TreeMap<Integer, String>();

        if (m.getImplementation() != null) {
            for (var t : m.getImplementation().getTryBlocks()) {
                String handlers = "";

                for (var h : t.getExceptionHandlers()) {
                    handlers += h.getExceptionType() + ":" + h.getHandlerCodeAddress() + ";";
                }

                for (int p = t.getStartCodeAddress();
                        p < t.getStartCodeAddress() + t.getCodeUnitCount();
                        p++) {
                    String old = out.put(p, handlers);
                    check(old == null || old.equals(handlers), "overlap");
                }
            }
        }

        return out;
    }

    static int proveCanonical(byte[] before, byte[] canonical) throws Exception {
        var old = classes(before);
        var now = classes(canonical);

        check(old.keySet().equals(now.keySet()), "canonical classes");
        int changed = 0;

        for (var e : old.entrySet()) {
            var c = now.get(e.getKey());

            if (FrameworkDexMaterializer.semantic(e.getValue())
                    .equals(FrameworkDexMaterializer.semantic(c))) {
                continue;
            }

            changed++;
            check(
                    FrameworkDexMaterializer.semantic(stripped(e.getValue()))
                            .equals(FrameworkDexMaterializer.semantic(stripped(c))),
                    "canonical changed beyond try blocks " + e.getKey());
            var ms = new TreeMap<String, Method>();

            for (var m : e.getValue().getMethods()) {
                ms.put(m.toString(), m);
            }

            for (var m : c.getMethods()) {
                check(
                        coverage(ms.get(m.toString())).equals(coverage(m)),
                        "canonical handler coverage " + m);
            }
        }

        return changed;
    }

    public static void main(String[] a) throws Exception {
        Path apk = Path.of(a[0]), job = Path.of(a[1]);
        byte[] before;

        try (var zip = new ZipFile(apk.toFile())) {
            before = zip.getInputStream(zip.getEntry("classes.dex")).readAllBytes();
        }

        var pins = new TreeMap<String, String>();

        for (var n : List.of("initVoiceClone", "showView")) {
            pins.put(n, FrameworkDexMaterializer.semantic(method(before, n)));
        }

        String pin = SettingsVoiceCloneGuard.hash(before);
        Files.writeString(job.resolve("method-pins.json"), "dex=" + pin + "\n" + pins + "\n");
        boolean red = false;

        try {
            run(method(before, "initVoiceClone"), new State(false), null);
        } catch (SecurityException e) {
            red = true;
        }

        check(red, "original absent must RED");
        byte[] after = SettingsVoiceCloneGuard.apply(STEP, 30, apk, before);
        State no = new State(false);
        run(method(after, "initVoiceClone"), no, null);
        hidden(no);
        check(
                no.helper == 0 && no.observer == 0 && no.cursor == 0 && no.clone == null,
                "absent effects");

        for (String v : List.of("my_voice", "mandarin", "dialect")) {
            run(method(after, "showView"), no, v);
            hidden(no);
        }

        checkPresentBehavior(before, after);
        checkLifecycle(after, no);
        byte[] canonical =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), classes(before).values()));
        int normalized = proveCanonical(before, canonical);
        Files.writeString(
                job.resolve("canonical-normalization.txt"),
                "normalized_classes="
                        + normalized
                        + "\n"
                        + "instruction_reference_debug_semantics=identical\n"
                        + "per_code_unit_exception_handler_coverage=identical\n");
        var c1 = classes(canonical);
        var c2 = classes(after);
        check(c1.keySet().equals(c2.keySet()), "class inventory");
        checkChangedClasses(c1, c2);
        int rejected = rejections(apk, before);
        Files.write(job.resolve("classes.dex"), after);
        String report =
                "original_absent=RED_SecurityException\n"
                    + "transformed_absent=no_helper_no_observer_no_cursor_clone_controls_hidden\n"
                    + "present_init_and_my_voice_mandarin_dialect=OEM_identical\n"
                    + "late_showView_callbacks=clone_hidden\n"
                    + "no_activity=OEM_identical\n"
                    + "onPause_onDestroy_without_helper=PASS\n"
                    + "changed_classes=1\n"
                    + "changed_methods=2\n"
                    + "inverse_guards="
                        + rejected
                        + "\nART=not_run\noutput_sha256="
                        + SettingsVoiceCloneGuard.hash(after)
                        + "\n";
        Files.writeString(job.resolve("regression-receipt.txt"), report);
        System.out.print(report);
    }

    static void checkPresentBehavior(byte[] before, byte[] after) {
        for (String name : List.of("initVoiceClone", "showView")) {
            for (String value :
                    name.equals("showView")
                            ? List.of("my_voice", "mandarin", "dialect")
                            : Collections.<String>singletonList(null)) {
                State old = new State(true), now = new State(true);
                run(method(before, name), old, value);
                run(method(after, name), now, value);
                check(
                        old.events.equals(now.events)
                                && old.view.equals(now.view)
                                && old.observer == now.observer,
                        "present behavior " + name + value);
            }
        }
    }

    static void checkLifecycle(byte[] after, State no) {
        State absentActivity = new State(false);

        absentActivity.activity = false;
        run(method(after, "initVoiceClone"), absentActivity, null);
        check(absentActivity.events.isEmpty(), "original noactivity unchanged");
        run(method(after, "onPause"), no, null);
        run(method(after, "onDestroy"), no, null);
        check(no.clone == null, "lifecycle absent helper");
    }

    static void checkChangedClasses(Map<String, ClassDef> c1, Map<String, ClassDef> c2)
            throws Exception {
        int changed = 0;

        for (var e : c1.entrySet()) {
            if (!FrameworkDexMaterializer.semantic(e.getValue())
                    .equals(FrameworkDexMaterializer.semantic(c2.get(e.getKey())))) {
                check(
                        e.getKey().equals(O),
                        "foreign class "
                                + e.getKey()
                                + " "
                                + FrameworkDexMaterializer.difference(
                                        e.getValue(), c2.get(e.getKey())));
                changed++;
                var ms = new TreeMap<String, String>();

                for (var m : e.getValue().getMethods()) {
                    ms.put(m.toString(), FrameworkDexMaterializer.semantic(m));
                }

                for (var m : c2.get(O).getMethods()) {
                    if (!SettingsVoiceCloneGuard.target(m)) {
                        check(
                                ms.get(m.toString()).equals(FrameworkDexMaterializer.semantic(m)),
                                "foreign method");
                    }
                }
            }
        }

        check(changed == 1, "one class");
    }

    static int rejections(Path apk, byte[] before) throws Exception {
        int rejected = 0;

        for (int i = 0; i < 3; i++) {
            try {
                byte[] x = before.clone();
                SettingsVoiceCloneGuard.apply(
                        i == 0 ? FOREIGN : STEP,
                        i == 1 ? 29 : 30,
                        i == 2 ? apk.resolveSibling("SystemService.apk") : apk,
                        x);
                throw new AssertionError("inverse accepted " + i);
            } catch (Failure e) {
                check(e.code.equals("SETTINGS_VOICE_CLONE"), "failure code");
                rejected++;
            }
        }

        return rejected;
    }
}
