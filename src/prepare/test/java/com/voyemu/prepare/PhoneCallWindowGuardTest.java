package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;
import org.jf.dexlib2.iface.reference.FieldReference;
import org.jf.dexlib2.iface.reference.MethodReference;
import org.jf.dexlib2.iface.reference.StringReference;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.TreeMap;

/**
 * Executes actual pinned original/transformed window-policy instructions; Android window service is
 * a boundary.
 */
public class PhoneCallWindowGuardTest {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1";

    static void need(boolean b, String s) {
        if (!b) {
            throw new AssertionError(s);
        }
    }

    static class Obj extends HashMap<String, Object> {
        String type;

        Obj(String t) {
            type = t;
        }
    }

    static class VM {
        Map<String, Method> methods = new HashMap<>();
        Obj manager = new Obj("manager"),
                full = new Obj("full"),
                small = new Obj("small"),
                event = new Obj("event");
        boolean foreground, wechat;
        int shows, closes, islands, updates;
        List<String> trace = new ArrayList<>();

        VM(byte[] bytes) {
            var dex = new DexBackedDexFile(Opcodes.forApi(30), bytes);

            for (var c : dex.getClasses()) {
                for (var m : c.getMethods()) {
                    if (c.getType().equals(PhoneCallWindowGuard.OWNER)) {
                        methods.put(m.getName(), m);
                    }
                }
            }

            manager.put("fullWindow", full);
            manager.put("smallWindow", small);
            manager.put("isManual", 0);
            manager.put("isFullScreen", 1);
            manager.put("mHandler", new Obj("handler"));
        }

        int num(Object o) {
            return o == null ? 0 : ((Number) o).intValue();
        }

        boolean zero(Object o) {
            return o == null || (o instanceof Number && num(o) == 0);
        }

        Object run(String name, Object... args) {
            var method = methods.get(name);

            need(method != null, "missing " + name);
            var impl = method.getImplementation();
            var code = new TreeMap<Integer, Instruction>();
            int p = 0;

            for (var i : impl.getInstructions()) {
                code.put(p, i);
                p += i.getCodeUnits();
            }

            Object[] r = new Object[impl.getRegisterCount()];
            int base = r.length - args.length;
            System.arraycopy(args, 0, r, base, args.length);
            Object result = null;
            int pc = 0;

            for (int steps = 0; steps < 1000; steps++) {
                var i = code.get(pc);
                need(i != null, "bad pc " + pc);
                int next = pc + i.getCodeUnits();
                int a = i instanceof OneRegisterInstruction x ? x.getRegisterA() : 0,
                        b = i instanceof TwoRegisterInstruction x ? x.getRegisterB() : 0;
                String ref = i instanceof ReferenceInstruction x ? x.getReference().toString() : "";

                switch (i.getOpcode()) {
                    case NOP, CHECK_CAST -> {}
                    case RETURN_VOID -> {
                        return null;
                    }
                    case CONST_4, CONST_16, CONST_WIDE_16 ->
                            r[a] = ((NarrowLiteralInstruction) i).getNarrowLiteral();
                    case CONST_STRING, CONST_STRING_JUMBO ->
                            r[a] =
                                    ((StringReference) ((ReferenceInstruction) i).getReference())
                                            .getString();
                    case NEW_INSTANCE -> r[a] = new Obj(ref);
                    case SGET_OBJECT -> r[a] = "TAG";
                    case MOVE_RESULT, MOVE_RESULT_OBJECT -> r[a] = result;
                    case IGET, IGET_BOOLEAN, IGET_OBJECT ->
                            r[a] =
                                    ((Obj) r[b])
                                            .get(
                                                    ((FieldReference)
                                                                    ((ReferenceInstruction) i)
                                                                            .getReference())
                                                            .getName());
                    case IPUT, IPUT_BOOLEAN, IPUT_OBJECT ->
                            ((Obj) r[b])
                                    .put(
                                            ((FieldReference)
                                                            ((ReferenceInstruction) i)
                                                                    .getReference())
                                                    .getName(),
                                            r[a]);
                    case IF_EQZ -> {
                        if (zero(r[a])) {
                            next = pc + ((OffsetInstruction) i).getCodeOffset();
                        }
                    }
                    case IF_NEZ -> {
                        if (!zero(r[a])) {
                            next = pc + ((OffsetInstruction) i).getCodeOffset();
                        }
                    }
                    case IF_EQ -> {
                        if (Objects.equals(r[a], r[b])) {
                            next = pc + ((OffsetInstruction) i).getCodeOffset();
                        }
                    }
                    case IF_NE -> {
                        if (!Objects.equals(r[a], r[b])) {
                            next = pc + ((OffsetInstruction) i).getCodeOffset();
                        }
                    }
                    case GOTO -> next = pc + ((OffsetInstruction) i).getCodeOffset();
                    case INVOKE_VIRTUAL, INVOKE_STATIC, INVOKE_DIRECT -> result = invoke(i, r);
                    default ->
                            throw new AssertionError("unsupported " + i.getOpcode() + " at " + pc);
                }

                pc = next;
            }

            throw new AssertionError("steps");
        }

        Object invoke(Instruction i, Object[] r) {
            var call = (MethodReference) ((ReferenceInstruction) i).getReference();
            var f = (FiveRegisterInstruction) i;
            int[] ids = {
                f.getRegisterC(),
                f.getRegisterD(),
                f.getRegisterE(),
                f.getRegisterF(),
                f.getRegisterG()
            };
            Object[] v = new Object[f.getRegisterCount()];

            for (int k = 0; k < v.length; k++) {
                v[k] = r[ids[k]];
            }

            String n = call.getName(), owner = call.getDefiningClass();
            Object result = null;

            if (owner.equals(PhoneCallWindowGuard.OWNER)) {
                trace.add(n);
                result = run(n, v);
            } else if (owner.contains("CallStateEvent;")) {
                result =
                        event.get(
                                switch (n) {
                                    case "getState" -> "state";
                                    case "getId" -> "id";
                                    case "getNumber" -> "number";
                                    default -> throw new AssertionError(n);
                                });
            } else if (n.equals("isAppOnForeground")) {
                result = foreground ? 1 : 0;
            } else if (n.equals("isWeChat")) {
                result = wechat ? 1 : 0;
            } else if (n.equals("getInstance")) {
                result = new Obj("app");
            } else if (n.equals("getDisplayId")) {
                result = 0;
            } else if (n.equals("updateView")) {
                updates++;

                if (num(event.get("state")) == 131) {
                    closes++;
                    full.put("visible", 0);
                    small.put("visible", 0);
                }
            } else if (n.equals("show")) {
                shows++;
                ((Obj) v[0]).put("visible", 1);
            } else if (n.equals("close")) {
                closes++;
                ((Obj) v[0]).put("visible", 0);
            } else if (n.equals("sendQGBusEvent")) {
                need(v[1].equals("show_island_card"), "unexpected bus");
                islands++;
            } else if (n.equals("append")) {
                result = v[0];
            } else if (n.equals("toString")) {
                result = "log";
            } else {
                need(
                        List.of("<init>", "i", "resetStatus", "sendEmptyMessageDelayed")
                                .contains(n),
                        "unexpected external " + call);
            }

            return result;
        }

        void state(int s) {
            event.put("state", s);
            event.put("id", 1);
            event.put("number", "+70000000001");
            run("updateCallState", manager, event);
        }
    }

    interface Check {
        void run() throws Exception;
    }

    static void rejects(Check c) throws Exception {
        try {
            c.run();
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError("guard accepted drift");
    }

    public static void main(String[] a) throws Exception {
        Path root = Path.of(a[0]), stage = Path.of(a[1]);

        Files.createDirectories(stage);
        Path apk = root.resolve(".local/profiles/" + PROFILE + "/originals/BluetoothPhone.apk");
        byte[] old = NativeCore.entries(apk).get("classes.dex"),
                fixed = PhoneCallWindowGuard.apply(PROFILE, 30, apk, old);
        PhoneCallWindowGuard.verify(PROFILE, old, fixed);
        int cases = 0;

        for (boolean fg : List.of(false, true)) {
            for (int state : List.of(128, 129, 130, 132)) {
                VM red = new VM(old);
                red.foreground = fg;
                red.state(state);
                need(red.shows == 0, "original must reproduce no renderer");
                VM green = new VM(fixed);
                green.foreground = fg;
                green.state(state);
                need(
                        green.shows == 1
                                && green.updates == 2
                                && green.islands == 0
                                && num(green.manager.get("isManual")) == 0,
                        "automatic renderer missing");
                green.state(state);
                need(green.shows == 2, "repeat routing");
                green.state(131);
                need(
                        num(green.full.get("visible")) == 0
                                && num(green.small.get("visible")) == 0
                                && num(green.manager.get("isManual")) == 0,
                        "terminal cleanup");
                cases++;
            }
        }

        for (boolean fg : List.of(false, true)) {
            VM v = new VM(fixed);
            v.foreground = fg;
            v.wechat = true;
            v.state(130);
            need(v.shows == 0 && v.islands == 1, "WeChat policy changed");
            v = new VM(fixed);
            v.foreground = fg;
            v.manager.put("isManual", 1);
            v.manager.put("isFullScreen", 0);
            v.state(130);
            need(
                    v.shows == 0 && v.islands == 1 && num(v.manager.get("isManual")) == 1,
                    "manual minimum changed");
            cases += 2;
        }

        VM nullEvent = new VM(fixed);
        nullEvent.run("updateCallState", nullEvent.manager, null);
        need(nullEvent.shows == 0 && nullEvent.updates == 0, "null behavior");
        rejects(() -> PhoneCallWindowGuard.apply("foreign", 30, apk, old));
        rejects(() -> PhoneCallWindowGuard.apply(PROFILE, 28, apk, old));
        rejects(() -> PhoneCallWindowGuard.apply(PROFILE, 30, apk, fixed));
        rejects(() -> PhoneCallWindowGuard.verify(PROFILE, old, old));
        byte[] drift = fixed.clone();
        drift[drift.length - 1] ^= 1;
        rejects(() -> PhoneCallWindowGuard.verify(PROFILE, old, drift));
        var before = new DexBackedDexFile(Opcodes.forApi(30), old);
        var after = new DexBackedDexFile(Opcodes.forApi(30), fixed);
        Map<String, String> semantics = new TreeMap<>();

        for (var c : before.getClasses()) {
            for (var m : c.getMethods()) {
                semantics.put(m.toString(), FrameworkDexMaterializer.semantic(m));
            }
        }

        int changed = 0, equal = 0;

        for (var c : after.getClasses()) {
            for (var m : c.getMethods()) {
                if (Objects.equals(
                        semantics.get(m.toString()), FrameworkDexMaterializer.semantic(m))) {
                    equal++;
                } else {
                    need(m.toString().equals(PhoneCallWindowGuard.METHOD), "extra mutation " + m);
                    changed++;
                }
            }
        }

        need(changed == 1, "changed inventory");
        Files.write(stage.resolve("classes.dex"), fixed);
        Files.writeString(
                stage.resolve("result.txt"),
                "original_foreground_and_background=RED_NO_RENDERER\n"
                        + "transformed_auto_128_129_130_132=GREEN_OEM_SHOW\n"
                        + "scenarios="
                        + cases
                        + "\n"
                        + "null_preserved=true\n"
                        + "manual_minimum_and_wechat_preserved=true\n"
                        + "terminal_131_preserved=true\n"
                        + "unchanged_methods="
                        + equal
                        + "\nchanged_methods="
                        + changed
                        + "\noriginal_sha256="
                        + NativeCore.hash(old)
                        + "\nprepared_sha256="
                        + NativeCore.hash(fixed)
                        + "\n"
                        + "negative_guards=5\n"
                        + "ART_and_actual_WindowManager_addView=NOT_EXECUTED\n");
        System.out.print(Files.readString(stage.resolve("result.txt")));
    }

    static int num(Object o) {
        return o == null ? 0 : ((Number) o).intValue();
    }
}
