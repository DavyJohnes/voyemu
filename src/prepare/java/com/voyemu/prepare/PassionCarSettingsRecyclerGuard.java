package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.debug.DebugItem;
import org.jf.dexlib2.iface.debug.EndLocal;
import org.jf.dexlib2.iface.debug.RestartLocal;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.debug.ImmutableEndLocal;
import org.jf.dexlib2.immutable.debug.ImmutableRestartLocal;
import org.jf.dexlib2.immutable.reference.ImmutableFieldReference;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;
import org.jf.dexlib2.immutable.reference.ImmutableTypeReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/** Reuse only the existing Car UI recycler, preserving the OEM fallback. */
final class PassionCarSettingsRecyclerGuard {
    static final String NAME = "passion-api30-settings-recycler";
    static final String PIN = "8d7798dd439ffa39d8af491ff23458d321c8c7659b53ea3672ab4d199189e4b4";
    static final String OWNER = "Landroidx/preference/PreferenceFragmentCompat;",
            CAR = "Lcom/android/car/ui/recyclerview/CarUiRecyclerView;";

    static void require(boolean ok) {
        Failure.require(
                ok,
                "PASSION_SETTINGS_RECYCLER_CONTRACT",
                "Exact Passion CarSettings recycler contract required",
                3);
    }

    static boolean target(Method m) {
        return m.getDefiningClass().equals(OWNER)
                && m.getName().equals("onCreateRecyclerView")
                && m.getParameterTypes()
                        .equals(
                                List.of(
                                        "Landroid/view/LayoutInflater;",
                                        "Landroid/view/ViewGroup;",
                                        "Landroid/os/Bundle;"))
                && m.getReturnType().equals("Landroidx/recyclerview/widget/RecyclerView;");
    }

    static Method method(DexFile dex) {
        Method found = null;

        for (var c : dex.getClasses()) {
            if (c.getType().equals(OWNER)) {
                for (var m : c.getVirtualMethods()) {
                    if (target(m)) {
                        require(found == null);
                        found = m;
                    }
                }
            }
        }

        require(found != null);

        return found;
    }

    static void validateMethod(Method m) throws Exception {
        require(
                target(m)
                        && m.getAccessFlags() == 1
                        && m.getImplementation() != null
                        && m.getImplementation().getRegisterCount() == 6
                        && m.getImplementation().getTryBlocks().isEmpty());
    }

    static void validate(String profile, int api, Path original, byte[] dex) throws Exception {
        require(api == 30 && FilesSupport.hash(original).equals(PIN) && dex != null);
        validateMethod(method(new DexBackedDexFile(Opcodes.forApi(30), dex)));
    }

    static Method route(Method m, boolean inverse) throws Exception {
        if (!inverse) {
            validateMethod(m);
        } else {
            require(target(m) && m.getImplementation() != null);
        }

        var b = new MutableMethodImplementation(m.getImplementation());

        if (inverse) {
            require(b.getInstructions().size() > 7);
            require(
                    b.getInstructions().get(0).getOpcode() == Opcode.SGET
                            && b.getInstructions().get(1).getOpcode() == Opcode.INVOKE_VIRTUAL
                            && b.getInstructions().get(2).getOpcode() == Opcode.MOVE_RESULT_OBJECT
                            && b.getInstructions().get(3).getOpcode() == Opcode.INSTANCE_OF
                            && b.getInstructions().get(4).getOpcode() == Opcode.IF_EQZ
                            && b.getInstructions().get(5).getOpcode() == Opcode.CHECK_CAST
                            && b.getInstructions().get(6).getOpcode() == Opcode.RETURN_OBJECT);

            for (int i = 0; i < 7; i++) {
                b.removeInstruction(0);
            }
        } else {
            var fallback = b.newLabelForIndex(0);
            b.addInstruction(
                    0,
                    new BuilderInstruction21c(
                            Opcode.SGET,
                            0,
                            new ImmutableFieldReference(
                                    "Landroidx/preference/R$id;", "recycler_view", "I")));
            b.addInstruction(
                    1,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_VIRTUAL,
                            2,
                            4,
                            0,
                            0,
                            0,
                            0,
                            new ImmutableMethodReference(
                                    "Landroid/view/ViewGroup;",
                                    "findViewById",
                                    List.of("I"),
                                    "Landroid/view/View;")));
            b.addInstruction(2, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0));
            b.addInstruction(
                    3,
                    new BuilderInstruction22c(
                            Opcode.INSTANCE_OF, 1, 0, new ImmutableTypeReference(CAR)));
            b.addInstruction(4, new BuilderInstruction21t(Opcode.IF_EQZ, 1, fallback));
            b.addInstruction(
                    5,
                    new BuilderInstruction21c(
                            Opcode.CHECK_CAST,
                            0,
                            new ImmutableTypeReference(
                                    "Landroidx/recyclerview/widget/RecyclerView;")));
            b.addInstruction(6, new BuilderInstruction11x(Opcode.RETURN_OBJECT, 0));
        }

        var debug = new ArrayList<DebugItem>();
        var old = m.getImplementation().getDebugItems().iterator();

        for (var item : b.getDebugItems()) {
            require(old.hasNext());
            var prior = old.next();
            require(prior.getDebugItemType() == item.getDebugItemType());

            if (item instanceof EndLocal e && prior instanceof EndLocal p) {
                debug.add(
                        new ImmutableEndLocal(
                                item.getCodeAddress(),
                                e.getRegister(),
                                p.getName(),
                                p.getType(),
                                p.getSignature()));
            } else if (item instanceof RestartLocal e && prior instanceof RestartLocal p) {
                debug.add(
                        new ImmutableRestartLocal(
                                item.getCodeAddress(),
                                e.getRegister(),
                                p.getName(),
                                p.getType(),
                                p.getSignature()));
            } else {
                debug.add(item);
            }
        }

        require(!old.hasNext());
        var implementation =
                new ImmutableMethodImplementation(
                        b.getRegisterCount(), b.getInstructions(), b.getTryBlocks(), debug);
        Method result =
                new ImmutableMethod(
                        m.getDefiningClass(),
                        m.getName(),
                        m.getParameters(),
                        m.getReturnType(),
                        m.getAccessFlags(),
                        m.getAnnotations(),
                        m.getHiddenApiRestrictions(),
                        implementation);

        if (inverse) {
            validateMethod(result);
        }

        return result;
    }

    static ImmutableDexFile rewrite(DexFile source, boolean inverse) throws Exception {
        var classes = new ArrayList<ClassDef>();
        int changed = 0;

        for (var c : source.getClasses()) {
            if (!c.getType().equals(OWNER)) {
                classes.add(c);
                continue;
            }

            var methods = new ArrayList<Method>();

            for (var m : c.getVirtualMethods()) {
                if (target(m)) {
                    m = route(m, inverse);
                    changed++;
                }

                methods.add(m);
            }

            classes.add(
                    new ImmutableClassDef(
                            c.getType(),
                            c.getAccessFlags(),
                            c.getSuperclass(),
                            c.getInterfaces(),
                            c.getSourceFile(),
                            c.getAnnotations(),
                            c.getStaticFields(),
                            c.getInstanceFields(),
                            c.getDirectMethods(),
                            methods));
        }

        require(changed == 1);

        return new ImmutableDexFile(Opcodes.forApi(30), classes);
    }

    static byte[] apply(String profile, int api, Path original, byte[] dex) throws Exception {
        validate(profile, api, original, dex);
        var source = new DexBackedDexFile(Opcodes.forApi(30), dex);
        var expected = rewrite(source, false);
        byte[] out = FrameworkDexMaterializer.serialize(expected);
        var actual = new DexBackedDexFile(Opcodes.forApi(30), out);
        require(
                FrameworkDexMaterializer.semantic(expected.getClasses())
                        .equals(FrameworkDexMaterializer.semantic(actual.getClasses())));
        require(
                FrameworkDexMaterializer.semantic(source.getClasses())
                        .equals(
                                FrameworkDexMaterializer.semantic(
                                        rewrite(actual, true).getClasses())));

        return out;
    }
}
