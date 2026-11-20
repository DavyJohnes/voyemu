package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public final class OriginalNaviVinGuard {
    static final String OWNER = "Lcom/qinggan/os/SystemProperties;",
            HELPER = "Lcom/voyemu/qgapi/BrokerVin;";

    static void need(boolean b, String why) {
        Failure.require(b, "ORIGINAL_NAVI_VIN", why, 3);
    }

    static String hash(byte[] b) throws Exception {
        return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(b));
    }

    static Map<String, ClassDef> classes(byte[] raw) {
        Map<String, ClassDef> c = new TreeMap<>();

        for (ClassDef x : new DexBackedDexFile(Opcodes.forApi(30), raw).getClasses()) {
            need(c.put(x.getType(), x) == null, "duplicate");
        }

        return c;
    }

    static Method method(ClassDef c) {
        Method found = null;

        for (Method m : c.getMethods()) {
            if (m.getName().equals("getVinCode")
                    && m.getParameterTypes().isEmpty()
                    && m.getReturnType().equals("Ljava/lang/String;")) {
                need(found == null, "duplicate method");
                found = m;
            }
        }

        need(
                found != null
                        && found.getAccessFlags() == 9
                        && found.getImplementation() != null
                        && found.getImplementation().getRegisterCount() == 3,
                "method signature");

        return found;
    }

    static void validateInput(String profile, int api, String abi, Path jar, byte[] raw)
            throws Exception {
        need(api == 30 && "arm64-v8a".equals(abi), "profile pin");
        need(
                "c178682be164be4872697dbb5accdc6ea28446941b6d681ddb736f2fdb627c07"
                        .equals(hash(Files.readAllBytes(jar))),
                "jar pin");
        need(raw != null, "original or predecessor dex");
    }

    static byte[] apply(String profile, int api, String abi, Path jar, byte[] raw)
            throws Exception {
        need(api == 30 && "arm64-v8a".equals(abi), "profile pin");
        need(
                "c178682be164be4872697dbb5accdc6ea28446941b6d681ddb736f2fdb627c07"
                        .equals(hash(Files.readAllBytes(jar))),
                "jar pin");
        need(raw != null, "predecessor dex");
        var old = classes(raw);
        ClassDef owner = old.get(OWNER);
        need(owner != null, "owner");
        Method m = method(owner);
        var b = new MutableMethodImplementation(m.getImplementation());
        int n = b.getInstructions().size(), returns = 0;

        for (var i : b.getInstructions()) {
            if (i.getOpcode() == Opcode.RETURN_OBJECT) {
                returns++;
            }
        }

        need(
                returns == 2
                        && n >= 2
                        && b.getInstructions().get(n - 2).getOpcode() == Opcode.CONST_4
                        && ((OneRegisterInstruction) b.getInstructions().get(n - 2)).getRegisterA()
                                == 1
                        && b.getInstructions().get(n - 1).getOpcode() == Opcode.RETURN_OBJECT
                        && ((OneRegisterInstruction) b.getInstructions().get(n - 1)).getRegisterA()
                                == 1,
                "return shape");
        b.replaceInstruction(
                n - 1,
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        1,
                        1,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                HELPER,
                                "select",
                                List.of("Ljava/lang/String;"),
                                "Ljava/lang/String;")));
        b.addInstruction(n, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 1));
        b.addInstruction(n + 1, new BuilderInstruction11x(Opcode.RETURN_OBJECT, 1));
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

        for (var c : old.values()) {
            if (!c.getType().equals(OWNER)) {
                out.put(c.getType(), ImmutableClassDef.of(c));
                continue;
            }

            List<Method> d = new ArrayList<>(), v = new ArrayList<>();

            for (Method x : c.getDirectMethods()) {
                d.add(
                        x.getName().equals("getVinCode")
                                        && x.getParameterTypes().isEmpty()
                                        && x.getReturnType().equals("Ljava/lang/String;")
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

        byte[] result =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), out.values()));
        result = HiddenApiOffsets.fix(raw, result);
        var actual = classes(result);
        need(actual.keySet().equals(out.keySet()), "class set");

        for (var e : out.entrySet()) {
            if (!FrameworkDexMaterializer.semantic(e.getValue())
                    .equals(FrameworkDexMaterializer.semantic(actual.get(e.getKey())))) {
                throw new Failure("ORIGINAL_NAVI_VIN", "semantic drift " + e.getKey(), 3);
            }
        }

        return result;
    }
}
