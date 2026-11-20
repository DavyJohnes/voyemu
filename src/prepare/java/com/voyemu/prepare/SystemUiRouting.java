package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

// Only the two original OEM status-bar add/remove paths need a display-specific WindowManager.
final class SystemUiRouting {
    static final String NAME = "api30-systemui-routing";
    static final String OWNER = "Lcom/qinggan/systemui/statusbar/phone/PhoneStatusBar;",
            ROUTER = "Lcom/voyemu/output/StatusBarRouter;";
    static final Set<String> METHODS = Set.of("addStatusBarWindow", "addStatusBarWindowSecond");

    private static void require(boolean b) {
        Failure.require(
                b, "SYSTEMUI_ROUTING", "Exact SystemUI status-bar routing contract required", 3);
    }

    static boolean target(Method m) {
        return METHODS.contains(m.getName())
                && m.getParameterTypes().equals(List.of("Z"))
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

    static void validate(Map<String, Object> constants, int api, Path original, byte[] dex)
            throws Exception {
        require(api == 30 && StepConstants.admits(constants, FilesSupport.hash(original)));
        require(dex != null);

        for (var m : methods(dex)) {
            require(
                    m.getAccessFlags() == 2
                            && m.getImplementation() != null
                            && m.getImplementation().getRegisterCount() == 4
                            && m.getImplementation().getTryBlocks().isEmpty());
        }
    }

    private static Method routed(Method m) {
        var body = new MutableMethodImplementation(m.getImplementation());
        int address = 0, replaced = 0;

        for (int i = 0; i < body.getInstructions().size(); i++) {
            var instruction = body.getInstructions().get(i);

            if (address == 0xf || address == 0x1e) {
                boolean add = address == 0xf;
                require(
                        instruction.getOpcode() == Opcode.INVOKE_INTERFACE
                                && instruction instanceof FiveRegisterInstruction
                                && instruction instanceof ReferenceInstruction);
                var old = (FiveRegisterInstruction) instruction;
                var reference = ((ReferenceInstruction) instruction).getReference().toString();
                require(
                        reference.equals(
                                        "Landroid/view/WindowManager;->"
                                                + (add
                                                        ? "addView(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V"
                                                        : "removeView(Landroid/view/View;)V"))
                                && old.getRegisterCount() == (add ? 3 : 2)
                                && old.getRegisterC() == 3
                                && old.getRegisterD() == 0
                                && (!add || old.getRegisterE() == 1));
                var params =
                        add
                                ? List.of(
                                        "Landroid/view/WindowManager;",
                                        "Landroid/view/View;",
                                        "Landroid/view/ViewGroup$LayoutParams;")
                                : List.of("Landroid/view/WindowManager;", "Landroid/view/View;");
                body.replaceInstruction(
                        i,
                        new BuilderInstruction35c(
                                Opcode.INVOKE_STATIC,
                                old.getRegisterCount(),
                                old.getRegisterC(),
                                old.getRegisterD(),
                                old.getRegisterE(),
                                old.getRegisterF(),
                                old.getRegisterG(),
                                new ImmutableMethodReference(
                                        ROUTER, add ? "addView" : "removeView", params, "V")));
                replaced++;
            }

            address += instruction.getCodeUnits();
        }

        require(replaced == 2);

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

    static byte[] apply(Map<String, Object> constants, int api, Path original, byte[] dex)
            throws Exception {
        validate(constants, api, original, dex);
        var classes = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            require(classes.put(c.getType(), c) == null);
        }

        ClassDef selected = classes.get(OWNER);
        var direct = new ArrayList<Method>();
        var virtual = new ArrayList<Method>();
        int replaced = 0;

        for (var m : selected.getDirectMethods()) {
            if (target(m)) {
                direct.add(routed(m));
                replaced++;
            } else {
                direct.add(m);
            }
        }

        for (var m : selected.getVirtualMethods()) {
            require(!target(m));
            virtual.add(m);
        }

        require(replaced == 2);
        classes.put(
                OWNER,
                new ImmutableClassDef(
                        selected.getType(),
                        selected.getAccessFlags(),
                        selected.getSuperclass(),
                        selected.getInterfaces(),
                        selected.getSourceFile(),
                        selected.getAnnotations(),
                        selected.getStaticFields(),
                        selected.getInstanceFields(),
                        direct,
                        virtual));
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

        require(StepConstants.admits(constants, FilesSupport.hash(original)));

        return output;
    }
}
