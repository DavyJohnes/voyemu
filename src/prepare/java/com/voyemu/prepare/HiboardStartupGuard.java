package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Map;
import java.util.TreeMap;
import java.util.zip.ZipFile;

/** Preserve existing component state until the real OEM light snapshot is available. */
final class HiboardStartupGuard {
    static final String
            OWNER = "Lcom/qinggan/app/hiboard/vehiclehiboard/main/ComponentSmallOTA1x1View;",
            TARGET = OWNER + "->updateUI(Z)V";

    static void require(boolean b) {
        Failure.require(b, "HIBOARD_STARTUP", "Exact API30 Hiboard startup consumer required", 3);
    }

    static Map<String, ClassDef> classes(byte[] d) {
        var out = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), d).getClasses()) {
            require(out.put(c.getType(), c) == null);
        }

        return out;
    }

    static Method target(Map<String, ClassDef> cs) {
        Method found = null;

        for (var c : cs.values()) {
            for (var m : c.getMethods()) {
                if (m.toString().equals(TARGET)) {
                    require(found == null);
                    found = m;
                }
            }
        }

        require(found != null);

        return found;
    }

    static int index(Method m, int address) {
        int at = 0, n = 0;

        for (var x : m.getImplementation().getInstructions()) {
            if (at == address) {
                return n;
            }

            at += x.getCodeUnits();
            n++;
        }

        throw new Failure("HIBOARD_STARTUP", "Instruction boundary missing", 3);
    }

    static void validate(Method m) throws Exception {
        require(
                m.toString().equals(TARGET)
                        && m.getAccessFlags() == 1
                        && m.getImplementation() != null
                        && m.getImplementation().getRegisterCount() == 14);
        var xs = new ArrayList<Instruction>();
        m.getImplementation().getInstructions().forEach(xs::add);
        var read = xs.get(index(m, 0x20d));
        var move = xs.get(index(m, 0x210));
        var deref = xs.get(index(m, 0x211));
        var tail = xs.get(index(m, 0xbfd));
        require(
                read.getOpcode() == Opcode.INVOKE_VIRTUAL
                        && ((ReferenceInstruction) read)
                                .getReference()
                                .toString()
                                .equals(
                                        "Lcom/qinggan/canbus/CanBusManager;->getLightStatus()Lcom/qinggan/canbus/LightStatus;")
                        && move.getOpcode() == Opcode.MOVE_RESULT_OBJECT
                        && ((OneRegisterInstruction) move).getRegisterA() == 13
                        && deref.getOpcode() == Opcode.INVOKE_VIRTUAL
                        && ((ReferenceInstruction) deref)
                                .getReference()
                                .toString()
                                .equals("Lcom/qinggan/canbus/LightStatus;->getFogRear()I")
                        && tail.getOpcode() == Opcode.INVOKE_DIRECT
                        && ((ReferenceInstruction) tail)
                                .getReference()
                                .toString()
                                .equals(OWNER + "->updateTextColor()V"));
    }

    static Method rewrite(Method m) throws Exception {
        validate(m);
        var code = new MutableMethodImplementation(m.getImplementation());
        var tail = code.newLabelForIndex(index(m, 0xbfd));
        code.addInstruction(index(m, 0x211), new BuilderInstruction21t(Opcode.IF_EQZ, 13, tail));

        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                code);
    }

    static void validateOriginalChain(
            Map<String, Object> constants, int api, Path original, byte[] clean) throws Exception {
        HiboardRouting.validate(constants, api, original, clean);
        byte[] routed = HiboardRouting.apply(constants, api, original, clean);
        validate(target(classes(routed)));
    }

    static void validateRouted(Map<String, Object> constants, int api, Path original, byte[] routed)
            throws Exception {
        require(api == 30 && StepConstants.admits(constants, FilesSupport.hash(original)));
        byte[] clean;

        try (var z = new ZipFile(original.toFile())) {
            clean = z.getInputStream(z.getEntry("classes.dex")).readAllBytes();
        }

        require(Arrays.equals(routed, HiboardRouting.apply(constants, api, original, clean)));
        validate(target(classes(routed)));
    }

    static byte[] apply(Map<String, Object> constants, int api, Path original, byte[] predecessor)
            throws Exception {
        require(api == 30 && StepConstants.admits(constants, FilesSupport.hash(original)));
        byte[] clean;

        try (var z = new ZipFile(original.toFile())) {
            clean = z.getInputStream(z.getEntry("classes.dex")).readAllBytes();
        }

        byte[] expected = HiboardRouting.apply(constants, api, original, clean);
        require(Arrays.equals(expected, predecessor));
        var before = classes(predecessor);
        var old = target(before);
        var replacement = rewrite(old);
        var out = new TreeMap<String, ClassDef>();

        for (var c : before.values()) {
            if (!c.getType().equals(OWNER)) {
                out.put(c.getType(), c);
                continue;
            }

            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();

            for (var m : c.getDirectMethods()) {
                direct.add(m);
            }

            for (var m : c.getVirtualMethods()) {
                virtual.add(m.toString().equals(TARGET) ? replacement : m);
            }

            out.put(
                    c.getType(),
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
                            virtual));
        }

        byte[] result =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), out.values()));
        var reread = classes(result);
        require(reread.keySet().equals(before.keySet()));

        for (var e : out.entrySet()) {
            require(
                    FrameworkDexMaterializer.semantic(e.getValue())
                            .equals(FrameworkDexMaterializer.semantic(reread.get(e.getKey()))));
        }

        require(StepConstants.admits(constants, FilesSupport.hash(original)));

        return result;
    }
}
