package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.TreeMap;

/** Only the pinned AOSP status-bar window is collapsed; OEM bars retain their own lifecycle. */
final class PassionStatusBarGuard {
    static final String PROFILE = "passion-11-russian-20260403-2.1.4.80",
            NAME = "passion-api30-statusbar";
    static final String PIN = "e66e289a7d5f7bd807a1310b91b83758a2eb8b1400dd882d59ebc57bab49c897";
    static final String OWNER = "Lcom/android/systemui/statusbar/phone/StatusBarWindowController;",
            ROUTER = "Lcom/voyemu/output/PassionStatusBarWindow;";
    static final Set<String> METHODS = Set.of("attach", "apply");

    private static void require(boolean b) {
        Failure.require(
                b,
                "PASSION_STATUSBAR_CONTRACT",
                "Exact Passion AOSP status-bar window contract required",
                3);
    }

    static boolean target(Method m) {
        return METHODS.contains(m.getName())
                && m.getParameterTypes()
                        .equals(
                                m.getName().equals("attach")
                                        ? List.of()
                                        : List.of(
                                                OWNER.substring(0, OWNER.length() - 1) + "$State;"))
                && m.getReturnType().equals("V");
    }

    static List<Method> methods(byte[] dex) {
        var out = new ArrayList<Method>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            if (c.getType().equals(OWNER)) {
                for (var m : c.getMethods()) {
                    if (target(m)) {
                        out.add(m);
                    }
                }
            }
        }

        require(out.size() == 2);

        return out;
    }

    static void validate(String profile, int api, Path original, byte[] dex) throws Exception {
        require(
                PROFILE.equals(profile)
                        && api == 30
                        && original != null
                        && FilesSupport.hash(original).equals(PIN));
        require(dex != null);

        for (var m : methods(dex)) {
            require(
                    m.getAccessFlags() == (m.getName().equals("attach") ? 1 : 2)
                            && m.getImplementation() != null
                            && m.getImplementation().getRegisterCount()
                                    == (m.getName().equals("attach") ? 8 : 3)
                            && m.getImplementation().getTryBlocks().isEmpty());
        }
    }

    static Method routed(Method m, boolean inverse) {
        var body = new MutableMethodImplementation(m.getImplementation());
        int address = 0, replaced = 0;

        for (int i = 0; i < body.getInstructions().size(); i++) {
            var instruction = body.getInstructions().get(i);

            if (address == (m.getName().equals("attach") ? 0x43 : 0x18)) {
                boolean add = m.getName().equals("attach");
                var old = requireWindowCall(instruction, add, inverse);
                body.replaceInstruction(i, windowCall(old, add, inverse));
                replaced++;
            }

            address += instruction.getCodeUnits();
        }

        require(replaced == 1);

        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                body);
    }

    private static FiveRegisterInstruction requireWindowCall(
            Instruction instruction, boolean add, boolean inverse) {
        require(
                instruction.getOpcode()
                                == (inverse ? Opcode.INVOKE_STATIC : Opcode.INVOKE_INTERFACE)
                        && instruction instanceof FiveRegisterInstruction
                        && instruction instanceof ReferenceInstruction);
        var old = (FiveRegisterInstruction) instruction;
        var reference = ((ReferenceInstruction) instruction).getReference().toString();
        require(
                reference.equals(
                                (inverse ? ROUTER : "Landroid/view/WindowManager;")
                                        + "->"
                                        + (add ? "addView" : "updateViewLayout")
                                        + (inverse
                                                ? "(Landroid/view/WindowManager;Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V"
                                                : "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V"))
                        && old.getRegisterCount() == 3
                        && old.getRegisterC() == (add ? 1 : 2)
                        && old.getRegisterD() == (add ? 2 : 0)
                        && old.getRegisterE() == (add ? 0 : 1));

        return old;
    }

    private static BuilderInstruction35c windowCall(
            FiveRegisterInstruction old, boolean add, boolean inverse) {
        var params =
                List.of(
                        "Landroid/view/WindowManager;",
                        "Landroid/view/View;",
                        "Landroid/view/ViewGroup$LayoutParams;");

        return new BuilderInstruction35c(
                inverse ? Opcode.INVOKE_INTERFACE : Opcode.INVOKE_STATIC,
                3,
                old.getRegisterC(),
                old.getRegisterD(),
                old.getRegisterE(),
                old.getRegisterF(),
                old.getRegisterG(),
                new ImmutableMethodReference(
                        inverse ? "Landroid/view/WindowManager;" : ROUTER,
                        add ? "addView" : "updateViewLayout",
                        inverse ? params.subList(1, 3) : params,
                        "V"));
    }

    static byte[] apply(String profile, int api, Path original, byte[] dex) throws Exception {
        validate(profile, api, original, dex);
        var classes = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            require(classes.put(c.getType(), c) == null);
        }

        classes.put(OWNER, routedOwner(classes.get(OWNER)));
        byte[] output =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), classes.values()));
        var read = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), output).getClasses()) {
            require(read.put(c.getType(), c) == null);
        }

        require(read.keySet().equals(classes.keySet()));

        for (var entry : classes.entrySet()) {
            require(
                    FrameworkDexMaterializer.semantic(entry.getValue())
                            .equals(FrameworkDexMaterializer.semantic(read.get(entry.getKey()))));
        }

        var originalClasses = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            originalClasses.put(c.getType(), c);
        }

        read.put(OWNER, restoredOwner(read.get(OWNER)));

        for (var e : originalClasses.entrySet()) {
            require(
                    FrameworkDexMaterializer.semantic(e.getValue())
                            .equals(FrameworkDexMaterializer.semantic(read.get(e.getKey()))));
        }

        require(FilesSupport.hash(original).equals(PIN));

        return output;
    }

    private static ClassDef routedOwner(ClassDef selected) {
        var direct = new ArrayList<Method>();
        var virtual = new ArrayList<Method>();
        int replaced = 0;

        for (var m : selected.getDirectMethods()) {
            if (target(m)) {
                direct.add(routed(m, false));
                replaced++;
            } else {
                direct.add(m);
            }
        }

        for (var m : selected.getVirtualMethods()) {
            if (target(m)) {
                virtual.add(routed(m, false));
                replaced++;
            } else {
                virtual.add(m);
            }
        }

        require(replaced == 2);

        return new ImmutableClassDef(
                selected.getType(),
                selected.getAccessFlags(),
                selected.getSuperclass(),
                selected.getInterfaces(),
                selected.getSourceFile(),
                selected.getAnnotations(),
                selected.getStaticFields(),
                selected.getInstanceFields(),
                direct,
                virtual);
    }

    private static ClassDef restoredOwner(ClassDef changed) {
        var restoredDirect = new ArrayList<Method>();
        var restoredVirtual = new ArrayList<Method>();

        for (var m : changed.getDirectMethods()) {
            restoredDirect.add(target(m) ? routed(m, true) : m);
        }

        for (var m : changed.getVirtualMethods()) {
            restoredVirtual.add(target(m) ? routed(m, true) : m);
        }

        return new ImmutableClassDef(
                changed.getType(),
                changed.getAccessFlags(),
                changed.getSuperclass(),
                changed.getInterfaces(),
                changed.getSourceFile(),
                changed.getAnnotations(),
                changed.getStaticFields(),
                changed.getInstanceFields(),
                restoredDirect,
                restoredVirtual);
    }
}
