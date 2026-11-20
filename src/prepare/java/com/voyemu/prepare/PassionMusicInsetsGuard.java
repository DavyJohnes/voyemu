package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11n;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21s;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * Fit the pinned Music activity below the OEM status bar and select its local OEM night palette.
 */
final class PassionMusicInsetsGuard {
    static final String NAME = "passion-api30-music-insets", PIN = PassionMusicForegroundGuard.PIN;
    static final String OWNER = "Lcom/voyah/multimedia/ui/MainActivity;";
    static final String IMMERSIVE =
            "Lcom/voyah/baselibrary/utils/WindowScreenUtil;->setTransparentImmersive(Landroid/view/Window;)V";
    static final String DECOR = "Landroid/view/Window;->getDecorView()Landroid/view/View;",
            VISIBILITY = "Landroid/view/View;->setSystemUiVisibility(I)V";
    static final int FIT_VISIBILITY = 0x300;
    static final String
            DELEGATE =
                    "Lcom/voyah/multimedia/ui/MainActivity;->getDelegate()Landroidx/appcompat/app/AppCompatDelegate;",
            NIGHT = "Landroidx/appcompat/app/AppCompatDelegate;->setLocalNightMode(I)V";

    private static void require(boolean ok) {
        Failure.require(
                ok,
                "PASSION_MUSIC_INSETS_CONTRACT",
                "Exact prepared Passion Music main-activity inset contract required",
                3);
    }

    static boolean target(Method m) {
        return m.getDefiningClass().equals(OWNER)
                && m.getName().equals("onCreate")
                && m.getParameterTypes().equals(List.of("Landroid/os/Bundle;"))
                && m.getReturnType().equals("V");
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
                        && m.getAccessFlags() == 4
                        && m.getImplementation() != null
                        && m.getImplementation().getRegisterCount() == 4
                        && m.getImplementation().getTryBlocks().isEmpty());
    }

    static void validate(String profile, int api, Path original, byte[] dex) throws Exception {
        require(api == 30 && FilesSupport.hash(original).equals(PIN) && dex != null);
        validateMethod(method(new DexBackedDexFile(Opcodes.forApi(30), dex)));
    }

    static void validateOriginalChain(String profile, int api, Path original, byte[] raw)
            throws Exception {
        validate(
                profile,
                api,
                original,
                PassionMusicForegroundGuard.apply(profile, api, original, raw));
    }

    private static boolean call(
            Instruction i, Opcode opcode, String reference, int count, int first, int second) {
        return i.getOpcode() == opcode
                && i instanceof ReferenceInstruction r
                && r.getReference().toString().equals(reference)
                && i instanceof FiveRegisterInstruction v
                && v.getRegisterCount() == count
                && v.getRegisterC() == first
                && (count == 1 || v.getRegisterD() == second);
    }

    static Method route(Method m, boolean inverse) throws Exception {
        if (!inverse) {
            validateMethod(m);
        } else {
            require(target(m) && m.getImplementation() != null);
        }

        var b = new MutableMethodImplementation(m.getImplementation());

        if (inverse) {
            var code = b.getInstructions();
            require(code.size() > 4 && call(code.get(0), Opcode.INVOKE_VIRTUAL, DELEGATE, 1, 2, 0));
            require(
                    code.get(1).getOpcode() == Opcode.MOVE_RESULT_OBJECT
                            && ((OneRegisterInstruction) code.get(1)).getRegisterA() == 0);
            require(
                    code.get(2).getOpcode() == Opcode.CONST_4
                            && ((OneRegisterInstruction) code.get(2)).getRegisterA() == 1
                            && ((NarrowLiteralInstruction) code.get(2)).getNarrowLiteral() == 2);
            require(call(code.get(3), Opcode.INVOKE_VIRTUAL, NIGHT, 2, 0, 1));

            for (int i = 0; i < 4; i++) {
                b.removeInstruction(0);
            }
        }

        int address = 0, index = -1;

        for (int i = 0; i < b.getInstructions().size(); i++) {
            if (address == 17) {
                index = i;
                break;
            }

            address += b.getInstructions().get(i).getCodeUnits();
        }

        require(
                index > 0
                        && call(
                                b.getInstructions().get(index - 1),
                                Opcode.INVOKE_STATIC,
                                IMMERSIVE,
                                1,
                                0,
                                0));

        if (inverse) {
            var instructions = b.getInstructions();
            require(
                    index + 4 < instructions.size()
                            && call(
                                    instructions.get(index),
                                    Opcode.INVOKE_VIRTUAL,
                                    DECOR,
                                    1,
                                    0,
                                    0));
            var result = instructions.get(index + 1);
            require(
                    result.getOpcode() == Opcode.MOVE_RESULT_OBJECT
                            && ((OneRegisterInstruction) result).getRegisterA() == 0);
            var constant = instructions.get(index + 2);
            require(
                    constant.getOpcode() == Opcode.CONST_16
                            && ((OneRegisterInstruction) constant).getRegisterA() == 1
                            && ((NarrowLiteralInstruction) constant).getNarrowLiteral()
                                    == FIT_VISIBILITY);
            require(call(instructions.get(index + 3), Opcode.INVOKE_VIRTUAL, VISIBILITY, 2, 0, 1));

            for (int i = 0; i < 4; i++) {
                b.removeInstruction(index);
            }
        } else {
            // v0 is the Window just passed to the OEM helper; v0/v1 are local registers.
            // Preserve LAYOUT_STABLE and LAYOUT_HIDE_NAVIGATION, clearing only LAYOUT_FULLSCREEN.
            b.addInstruction(
                    index++,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_VIRTUAL,
                            1,
                            0,
                            0,
                            0,
                            0,
                            0,
                            new ImmutableMethodReference(
                                    "Landroid/view/Window;",
                                    "getDecorView",
                                    List.of(),
                                    "Landroid/view/View;")));
            b.addInstruction(index++, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0));
            b.addInstruction(
                    index++, new BuilderInstruction21s(Opcode.CONST_16, 1, FIT_VISIBILITY));
            b.addInstruction(
                    index,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_VIRTUAL,
                            2,
                            0,
                            1,
                            0,
                            0,
                            0,
                            new ImmutableMethodReference(
                                    "Landroid/view/View;",
                                    "setSystemUiVisibility",
                                    List.of("I"),
                                    "V")));
        }

        if (!inverse) {
            addNightMode(b);
        }

        Method result =
                new ImmutableMethod(
                        m.getDefiningClass(),
                        m.getName(),
                        m.getParameters(),
                        m.getReturnType(),
                        m.getAccessFlags(),
                        m.getAnnotations(),
                        m.getHiddenApiRestrictions(),
                        b);

        if (inverse) {
            validateMethod(result);
        }

        return result;
    }

    // Prepends getDelegate().setLocalNightMode(2) after verifying the super.onCreate entry call.
    private static void addNightMode(MutableMethodImplementation b) {
        require(
                call(
                        b.getInstructions().get(0),
                        Opcode.INVOKE_SUPER,
                        "Landroidx/appcompat/app/AppCompatActivity;->onCreate(Landroid/os/Bundle;)V",
                        2,
                        2,
                        3));
        b.addInstruction(
                0,
                new BuilderInstruction35c(
                        Opcode.INVOKE_VIRTUAL,
                        1,
                        2,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                OWNER,
                                "getDelegate",
                                List.of(),
                                "Landroidx/appcompat/app/AppCompatDelegate;")));
        b.addInstruction(1, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0));
        b.addInstruction(2, new BuilderInstruction11n(Opcode.CONST_4, 1, 2));
        b.addInstruction(
                3,
                new BuilderInstruction35c(
                        Opcode.INVOKE_VIRTUAL,
                        2,
                        0,
                        1,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                "Landroidx/appcompat/app/AppCompatDelegate;",
                                "setLocalNightMode",
                                List.of("I"),
                                "V")));
    }

    static void validateAfter(Method m) throws Exception {
        route(m, true);
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
        validateAfter(method(actual));
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
