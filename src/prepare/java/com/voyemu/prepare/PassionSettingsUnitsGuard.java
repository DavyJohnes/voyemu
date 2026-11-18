package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Set;

/** Keep OEM units unavailable when the Android Car service is absent, without a lifecycle crash. */
final class PassionSettingsUnitsGuard {
    static final String PROFILE = PassionSettingsSoundGuard.PROFILE,
            APK = PassionSettingsSoundGuard.APK,
            NAME = "passion-api30-settings-units";
    static final String OWNER = "Lcom/android/car/settings/units/CarUnitsManager;";
    static final Set<String> METHODS = Set.of("connect", "disconnect");

    static void need(boolean b) {
        Failure.require(b, "PASSION_UNITS_CONTRACT", "Exact Passion Units lifecycle required", 3);
    }

    static boolean target(Method m) {
        return m.getDefiningClass().equals(OWNER)
                && METHODS.contains(m.getName())
                && m.getParameterTypes().isEmpty()
                && m.getReturnType().equals("V");
    }

    static void validateMethod(Method m) throws Exception {
        need(
                target(m)
                        && m.getAccessFlags() == 4
                        && m.getImplementation() != null
                        && m.getImplementation().getRegisterCount() == 2
                        && m.getImplementation().getTryBlocks().isEmpty());
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
                for (var m : c.getVirtualMethods()) {
                    if (target(m)) {
                        validateMethod(m);
                        n++;
                    }
                }
            }
        }

        need(n == 2);
    }

    static void validateOriginalChain(String profile, int api, Path apk, byte[] raw)
            throws Exception {
        validate(profile, api, apk, PassionSettingsSoundGuard.apply(profile, api, apk, raw));
    }

    static Method route(Method m, boolean inverse) throws Exception {
        if (!inverse) {
            validateMethod(m);
        } else {
            need(target(m));
        }

        var b = new MutableMethodImplementation(m.getImplementation());

        if (inverse) {
            var i = b.getInstructions().get(1);
            need(
                    i.getOpcode() == Opcode.IF_EQZ
                            && ((OneRegisterInstruction) i).getRegisterA() == 0
                            && ((OffsetInstruction) i).getCodeOffset() == 5);
            b.removeInstruction(1);
        } else {
            var end = b.newLabelForIndex(2);
            b.addInstruction(1, new BuilderInstruction21t(Opcode.IF_EQZ, 0, end));
        }

        var result =
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

    static ImmutableDexFile rewrite(DexFile dex, boolean inverse) throws Exception {
        var classes = new ArrayList<ClassDef>();
        int n = 0;

        for (var c : dex.getClasses()) {
            if (!c.getType().equals(OWNER)) {
                classes.add(c);
                continue;
            }

            var methods = new ArrayList<Method>();

            for (var m : c.getVirtualMethods()) {
                if (target(m)) {
                    m = route(m, inverse);
                    n++;
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

        need(n == 2);

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
