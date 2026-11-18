package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11n;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction30t;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.debug.DebugItem;
import org.jf.dexlib2.iface.debug.EndLocal;
import org.jf.dexlib2.iface.debug.RestartLocal;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.debug.ImmutableEndLocal;
import org.jf.dexlib2.immutable.debug.ImmutableRestartLocal;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/** Only the exact emulated Passion camera may fall back after exhausting EXTERNAL cameras. */
final class PassionCameraVirtualGuard {
    static final String PROFILE = "passion-11-russian-20260403-2.1.4.80",
            NAME = "passion-api30-camera-virtual",
            APK = "e0e57f7cd9099136541c3ed35e1042fba1762a13640bea7f6a2ac3415f426bf0",
            OWNER = "Lcom/voyah/camera/camera/CameraPreviewManager;";

    static void need(boolean b) {
        Failure.require(
                b, "PASSION_CAMERA_CONTRACT", "Exact Passion emulated Camera search required", 3);
    }

    static boolean target(Method m) {
        return m.getDefiningClass().equals(OWNER)
                && m.getName().equals("getCameraIdByFacing")
                && m.getParameterTypes()
                        .equals(List.of("Landroid/hardware/camera2/CameraManager;", "I"))
                && m.getReturnType().equals("Ljava/lang/String;");
    }

    static void validateMethod(Method m) throws Exception {
        need(
                target(m)
                        && m.getAccessFlags() == 2
                        && m.getImplementation() != null
                        && m.getImplementation().getRegisterCount() == 9);
    }

    static void validate(String profile, int api, Path apk, byte[] bytes) throws Exception {
        need(
                PROFILE.equals(profile)
                        && api == 30
                        && FilesSupport.hash(apk).equals(APK)
                        && bytes != null);
        int n = 0;

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), bytes).getClasses()) {
            if (c.getType().equals(OWNER)) {
                for (var m : c.getDirectMethods()) {
                    if (target(m)) {
                        validateMethod(m);
                        n++;
                    }
                }
            }
        }

        need(n == 1);
    }

    static void validateOriginalChain(String profile, int api, Path apk, byte[] raw)
            throws Exception {
        validate(profile, api, apk, PassionServiceForegroundGuard.apply(profile, api, apk, raw));
    }

    static Method route(Method m, boolean inverse) throws Exception {
        if (!inverse) {
            validateMethod(m);
        } else {
            need(target(m));
        }

        var b = new MutableMethodImplementation(m.getImplementation());
        int address = 0, loop = -1, normal = -1;

        for (int i = 0; i < b.getInstructions().size(); i++) {
            if (address == 11) {
                loop = i;
            }

            if (address == 55) {
                normal = i;
            }

            address += b.getInstructions().get(i).getCodeUnits();
        }

        need(loop >= 0 && normal > loop);
        var branch = b.getInstructions().get(loop);
        need(
                branch.getOpcode() == Opcode.IF_GE
                        && ((TwoRegisterInstruction) branch).getRegisterA() == 2
                        && ((TwoRegisterInstruction) branch).getRegisterB() == 1);
        var normalLabel = b.newLabelForIndex(normal);
        var loopLabel = b.newLabelForIndex(loop);

        if (inverse) {
            requireFallbackTail(b);
            b.replaceInstruction(loop, new BuilderInstruction22t(Opcode.IF_GE, 2, 1, normalLabel));

            for (int j = 0; j < 5; j++) {
                b.removeInstruction(b.getInstructions().size() - 1);
            }
        } else {
            var fallback = b.newLabelForIndex(b.getInstructions().size());
            b.replaceInstruction(loop, new BuilderInstruction22t(Opcode.IF_GE, 2, 1, fallback));
            b.addInstruction(new BuilderInstruction11n(Opcode.CONST_4, 3, 2));
            b.addInstruction(new BuilderInstruction22t(Opcode.IF_NE, 8, 3, normalLabel));
            b.addInstruction(new BuilderInstruction11n(Opcode.CONST_4, 8, 1));
            b.addInstruction(new BuilderInstruction11n(Opcode.CONST_4, 2, 0));
            b.addInstruction(new BuilderInstruction30t(Opcode.GOTO_32, loopLabel));
        }

        var debug = relocatedDebugItems(m, b);
        var impl =
                new ImmutableMethodImplementation(
                        b.getRegisterCount(),
                        b.getInstructions(),
                        m.getImplementation().getTryBlocks(),
                        debug);
        var result =
                new ImmutableMethod(
                        m.getDefiningClass(),
                        m.getName(),
                        m.getParameters(),
                        m.getReturnType(),
                        m.getAccessFlags(),
                        m.getAnnotations(),
                        m.getHiddenApiRestrictions(),
                        impl);

        if (inverse) {
            validateMethod(result);
        }

        return result;
    }

    private static void requireFallbackTail(MutableMethodImplementation b) {
        int n = b.getInstructions().size();

        need(n > 5);
        var code = b.getInstructions();
        need(
                code.get(n - 5).getOpcode() == Opcode.CONST_4
                        && ((OneRegisterInstruction) code.get(n - 5)).getRegisterA() == 3
                        && ((NarrowLiteralInstruction) code.get(n - 5)).getNarrowLiteral() == 2);
        need(
                code.get(n - 4).getOpcode() == Opcode.IF_NE
                        && ((TwoRegisterInstruction) code.get(n - 4)).getRegisterA() == 8
                        && ((TwoRegisterInstruction) code.get(n - 4)).getRegisterB() == 3);
        need(
                code.get(n - 3).getOpcode() == Opcode.CONST_4
                        && ((OneRegisterInstruction) code.get(n - 3)).getRegisterA() == 8
                        && ((NarrowLiteralInstruction) code.get(n - 3)).getNarrowLiteral() == 1);
        need(
                code.get(n - 2).getOpcode() == Opcode.CONST_4
                        && ((OneRegisterInstruction) code.get(n - 2)).getRegisterA() == 2
                        && ((NarrowLiteralInstruction) code.get(n - 2)).getNarrowLiteral() == 0);
        need(code.get(n - 1).getOpcode() == Opcode.GOTO_32);
    }

    private static List<DebugItem> relocatedDebugItems(Method m, MutableMethodImplementation b) {
        var debug = new ArrayList<DebugItem>();
        var previous = m.getImplementation().getDebugItems().iterator();

        for (var item : b.getDebugItems()) {
            need(previous.hasNext());
            var old = previous.next();
            need(old.getDebugItemType() == item.getDebugItemType());

            if (item instanceof EndLocal e && old instanceof EndLocal y) {
                debug.add(
                        new ImmutableEndLocal(
                                item.getCodeAddress(),
                                e.getRegister(),
                                y.getName(),
                                y.getType(),
                                y.getSignature()));
            } else if (item instanceof RestartLocal e && old instanceof RestartLocal y) {
                debug.add(
                        new ImmutableRestartLocal(
                                item.getCodeAddress(),
                                e.getRegister(),
                                y.getName(),
                                y.getType(),
                                y.getSignature()));
            } else {
                debug.add(item);
            }
        }

        need(!previous.hasNext());

        return debug;
    }

    static ImmutableDexFile rewrite(DexFile dex, boolean inverse) throws Exception {
        var classes = new ArrayList<ClassDef>();
        int n = 0;

        for (var c : dex.getClasses()) {
            if (!c.getType().equals(OWNER)) {
                classes.add(c);
                continue;
            }

            var direct = new ArrayList<Method>();

            for (var m : c.getDirectMethods()) {
                if (target(m)) {
                    m = route(m, inverse);
                    n++;
                }

                direct.add(m);
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
                            direct,
                            c.getVirtualMethods()));
        }

        need(n == 1);

        return new ImmutableDexFile(Opcodes.forApi(30), classes);
    }

    static byte[] apply(String profile, int api, Path apk, byte[] bytes) throws Exception {
        validate(profile, api, apk, bytes);
        var before = new DexBackedDexFile(Opcodes.forApi(30), bytes);
        var expected = rewrite(before, false);
        byte[] out = FrameworkDexMaterializer.serialize(expected);
        var actual = new DexBackedDexFile(Opcodes.forApi(30), out);
        need(
                FrameworkDexMaterializer.semantic(expected.getClasses())
                        .equals(FrameworkDexMaterializer.semantic(actual.getClasses())));
        need(
                FrameworkDexMaterializer.semantic(before.getClasses())
                        .equals(
                                FrameworkDexMaterializer.semantic(
                                        rewrite(actual, true).getClasses())));

        return out;
    }
}
