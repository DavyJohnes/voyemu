package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Map;
import java.util.TreeMap;

/** A restored Electric fragment may be destroyed before its binding exists. */
final class PassionVehicleAnimationGuard {
    static final String PROFILE = "passion-11-russian-20260403-2.1.4.80",
            NAME = "passion-api30-vehicle-animation";
    static final String PIN = "a5fb5daed4a295a568daf0390858a0bd00782b1384e66b13a96f66d43ad43e60";
    static final String OWNER = "Lcom/voyah/car/ui/power/ElectricFragmentN;",
            METHOD = "clearAnimation";

    static void need(boolean value) {
        Failure.require(
                value,
                "PASSION_VEHICLE_ANIMATION_CONTRACT",
                "Exact Passion Electric animation binding cleanup required",
                3);
    }

    static Map<String, ClassDef> classes(byte[] dex) {
        return PassionServiceForegroundGuard.classes(dex);
    }

    static Method method(Map<String, ClassDef> classes) {
        ClassDef type = classes.get(OWNER);

        need(type != null);
        Method found = null;

        for (var m : type.getDirectMethods()) {
            if (m.getName().equals(METHOD)) {
                need(found == null);
                found = m;
            }
        }

        need(found != null);

        return found;
    }

    static void validateMethod(Method m) throws Exception {
        need(
                m.getDefiningClass().equals(OWNER)
                        && m.getName().equals(METHOD)
                        && m.getAccessFlags() == 2
                        && m.getParameterTypes().isEmpty()
                        && m.getReturnType().equals("V"));
        var body = m.getImplementation();
        need(body != null && body.getRegisterCount() == 3 && body.getTryBlocks().isEmpty());
        int units = 0;

        for (var i : body.getInstructions()) {
            units += i.getCodeUnits();
        }

        need(units == 29);
    }

    static Method rewriteMethod(Method m, boolean inverse) throws Exception {
        if (!inverse) {
            validateMethod(m);
        }

        var body = new MutableMethodImplementation(m.getImplementation());
        int at = 0, index = -1;

        for (int i = 0; i < body.getInstructions().size(); i++) {
            if (at == 12) {
                index = i;
            }

            at += body.getInstructions().get(i).getCodeUnits();
        }

        need(index >= 0 && at == (inverse ? 31 : 29));

        if (inverse) {
            var branch = body.getInstructions().get(index);
            need(
                    branch.getOpcode() == Opcode.IF_EQZ
                            && ((OneRegisterInstruction) branch).getRegisterA() == 0
                            && ((OffsetInstruction) branch).getCodeOffset() == 9);
            body.removeInstruction(index);
        } else {
            var cleanup = body.newLabelForAddress(19);
            body.addInstruction(index, new BuilderInstruction21t(Opcode.IF_EQZ, 0, cleanup));
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
                        body);

        if (inverse) {
            validateMethod(result);
        }

        return result;
    }

    static Map<String, ClassDef> rewrite(Map<String, ClassDef> source, boolean inverse)
            throws Exception {
        var result = new TreeMap<>(source);
        ClassDef type = source.get(OWNER);

        need(type != null);
        var direct = new ArrayList<Method>();
        int changed = 0;

        for (var m : type.getDirectMethods()) {
            if (m.getName().equals(METHOD)) {
                m = rewriteMethod(m, inverse);
                changed++;
            }

            direct.add(m);
        }

        need(changed == 1);
        result.put(
                OWNER,
                new ImmutableClassDef(
                        type.getType(),
                        type.getAccessFlags(),
                        type.getSuperclass(),
                        type.getInterfaces(),
                        type.getSourceFile(),
                        type.getAnnotations(),
                        type.getStaticFields(),
                        type.getInstanceFields(),
                        direct,
                        type.getVirtualMethods()));

        return result;
    }

    static void same(Map<String, ClassDef> expected, Map<String, ClassDef> actual)
            throws Exception {
        need(expected.keySet().equals(actual.keySet()));

        for (String name : expected.keySet()) {
            need(
                    FrameworkDexMaterializer.semantic(expected.get(name))
                            .equals(FrameworkDexMaterializer.semantic(actual.get(name))));
        }
    }

    static void validateInput(String profile, int api, Path original, byte[] dex) throws Exception {
        need(
                PROFILE.equals(profile)
                        && api == 30
                        && original != null
                        && FilesSupport.hash(original).equals(PIN)
                        && dex != null);
    }

    static void validatePrepared(String profile, int api, Path original, byte[] dex)
            throws Exception {
        validateInput(profile, api, original, dex);
        validateMethod(method(classes(dex)));
    }

    static byte[] prepareOriginal(String profile, int api, Path original, byte[] raw)
            throws Exception {
        validateInput(profile, api, original, raw);
        byte[] prior = PassionVehicleAudioGuard.apply(profile, api, original, raw);
        prior = PassionBluetoothVolumeGuard.apply(profile, api, original, prior);
        prior = PassionAmbientLightGuard.apply(profile, api, original, prior);
        validatePrepared(profile, api, original, prior);

        return prior;
    }

    static void validateOriginalChain(String profile, int api, Path original, byte[] raw)
            throws Exception {
        prepareOriginal(profile, api, original, raw);
    }

    static byte[] applyPrepared(String profile, int api, Path original, byte[] dex)
            throws Exception {
        validatePrepared(profile, api, original, dex);
        var before = classes(dex);
        var expected = rewrite(before, false);
        byte[] output =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), expected.values()));
        var actual = classes(output);
        same(expected, actual);
        same(before, rewrite(actual, true));

        return output;
    }
}
