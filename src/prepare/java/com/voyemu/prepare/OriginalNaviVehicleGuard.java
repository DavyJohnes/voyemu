package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21s;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableFieldReference;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public class OriginalNaviVehicleGuard {
    static final String OWNER = "Lcom/qinggan/vehicle/VehicleConfigHelper;",
            HELPER = "Lcom/voyemu/qgapi/VehicleBroker;";

    static void need(boolean b, String s) {
        Failure.require(b, "ORIGINAL_NAVI_VEHICLE", s, 3);
    }

    static String hash(byte[] b) throws Exception {
        return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(b));
    }

    static Map<String, ClassDef> classes(byte[] raw) {
        var a = new TreeMap<String, ClassDef>();

        for (ClassDef c : new DexBackedDexFile(Opcodes.forApi(30), raw).getClasses()) {
            need(a.put(c.getType(), c) == null, "duplicate");
        }

        return a;
    }

    static Method method(ClassDef c) {
        Method found = null;

        for (Method m : c.getMethods()) {
            if (m.getName().equals("getVehicleTypeID")
                    && m.getParameterTypes().isEmpty()
                    && m.getReturnType().equals("I")) {
                need(found == null, "duplicate");
                found = m;
            }
        }

        need(
                found != null
                        && found.getAccessFlags() == 9
                        && found.getImplementation() != null
                        && found.getImplementation().getRegisterCount() == 10,
                "signature");

        return found;
    }

    static byte[] apply(String profile, int api, String abi, Path jar, byte[] raw)
            throws Exception {
        need(
                "free-11-chinese-20260731-7.3.1".equals(profile)
                        && api == 30
                        && "arm64-v8a".equals(abi),
                "profile");
        need(
                "d95d1b05418660d278fbaf2e6e390f8849d3dd23555a38d8d1965efb85cccede"
                                .equals(hash(Files.readAllBytes(jar)))
                        && raw != null,
                "pin");
        var before = classes(raw);
        ClassDef owner = before.get(OWNER);
        need(owner != null, "owner");
        Method m = method(owner);
        var b = new MutableMethodImplementation(m.getImplementation());
        var ins = b.getInstructions();
        need(
                ins.size() > 5
                        && ins.get(0).getOpcode() == Opcode.SGET
                        && ins.get(1).getOpcode() == Opcode.IF_EQZ
                        && ins.get(2).getOpcode() == Opcode.RETURN
                        && ins.get(3).getOpcode() == Opcode.CONST_16
                        && ((OneRegisterInstruction) ins.get(3)).getRegisterA() == 0
                        && ((NarrowLiteralInstruction) ins.get(3)).getNarrowLiteral() == 43,
                "shape");
        need(
                ((ReferenceInstruction) ins.get(0))
                        .getReference()
                        .toString()
                        .equals(OWNER + "->sVehicleID:I"),
                "cache");
        b.replaceInstruction(
                3,
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(HELPER, "lookup", List.of(), "I")));
        b.addInstruction(4, new BuilderInstruction11x(Opcode.MOVE_RESULT, 0));
        b.addInstruction(5, new BuilderInstruction10x(Opcode.NOP));
        b.addInstruction(
                6,
                new BuilderInstruction21c(
                        Opcode.SPUT, 0, new ImmutableFieldReference(OWNER, "sVehicleID", "I")));
        b.addInstruction(7, new BuilderInstruction11x(Opcode.RETURN, 0));
        b.addInstruction(8, new BuilderInstruction21s(Opcode.CONST_16, 0, 43));
        b.replaceInstruction(5, new BuilderInstruction21t(Opcode.IF_EQZ, 0, b.newLabelForIndex(8)));
        Method changed =
                new ImmutableMethod(
                        m.getDefiningClass(),
                        m.getName(),
                        m.getParameters(),
                        m.getReturnType(),
                        m.getAccessFlags(),
                        m.getAnnotations(),
                        m.getHiddenApiRestrictions(),
                        b);
        Map<String, ClassDef> out = new TreeMap<>();

        for (var c : before.values()) {
            if (!c.getType().equals(OWNER)) {
                out.put(c.getType(), ImmutableClassDef.of(c));
                continue;
            }

            List<Method> d = new ArrayList<>(), v = new ArrayList<>();

            for (Method x : c.getDirectMethods()) {
                d.add(
                        x.getName().equals("getVehicleTypeID")
                                        && x.getParameterTypes().isEmpty()
                                        && x.getReturnType().equals("I")
                                ? changed
                                : x);
            }

            for (Method x : c.getVirtualMethods()) {
                v.add(x);
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
                            d,
                            v));
        }

        byte[] encoded =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), out.values()));
        encoded = HiddenApiOffsets.fix(raw, encoded);
        var after = classes(encoded);
        need(after.keySet().equals(out.keySet()), "classes");

        for (var e : out.entrySet()) {
            if (!FrameworkDexMaterializer.semantic(e.getValue())
                    .equals(FrameworkDexMaterializer.semantic(after.get(e.getKey())))) {
                throw new Failure("ORIGINAL_NAVI_VEHICLE", "semantic drift " + e.getKey(), 3);
            }
        }

        return encoded;
    }
}
