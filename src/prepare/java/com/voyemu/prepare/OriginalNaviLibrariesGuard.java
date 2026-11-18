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
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

final class OriginalNaviLibrariesGuard {
    static final String
            OWNER = "Lcom/android/server/pm/parsing/library/PackageBackwardCompatibility;",
            PKG = "Lcom/android/server/pm/parsing/pkg/ParsedPackage;";

    static void need(boolean b) {
        Failure.require(
                b, "ORIGINAL_NAVI_LIBRARIES", "Exact original Navi libraries contract required", 3);
    }

    static boolean hidden(byte[] b) {
        var x = java.nio.ByteBuffer.wrap(b).order(java.nio.ByteOrder.LITTLE_ENDIAN);

        need(b.length >= 112 && x.getInt(32) == b.length);
        int m = x.getInt(52);
        need(m >= 112 && m + 4L <= b.length);
        int n = x.getInt(m);
        need(n > 0 && n < 1000 && m + 4L + 12L * n <= b.length);

        for (int i = 0; i < n; i++) {
            if ((x.getShort(m + 4 + 12 * i) & 65535) == 0xf000) {
                return true;
            }
        }

        return false;
    }

    static byte[] apply(String profile, int api, byte[] b) throws Exception {
        need("free-11-chinese-20260731-7.3.1".equals(profile) && api == 30 && b != null);
        var dex = new DexBackedDexFile(Opcodes.forApi(30), b);
        Map<String, ClassDef> all = new TreeMap<>();

        for (ClassDef c : dex.getClasses()) {
            need(all.put(c.getType(), c) == null);
        }

        ClassDef target = all.get(OWNER);
        need(target != null && target.getAccessFlags() == 1);
        Method found = sharedLibrariesMethod(target);
        Method replacement = hookedSharedLibraries(found);
        Map<String, ClassDef> out = new TreeMap<>();

        for (ClassDef c : all.values()) {
            if (!c.getType().equals(OWNER)) {
                out.put(c.getType(), ImmutableClassDef.of(c));
                continue;
            }

            List<Method> direct = new ArrayList<>();

            for (Method m : c.getDirectMethods()) {
                direct.add(m.getName().equals("modifySharedLibraries") ? replacement : m);
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
                            c.getVirtualMethods()));
        }

        byte[] result =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), out.values()));
        need(!hidden(b) && !hidden(result));
        var reread = new DexBackedDexFile(Opcodes.forApi(30), result);
        Map<String, ClassDef> actual = new TreeMap<>();

        for (ClassDef c : reread.getClasses()) {
            need(actual.put(c.getType(), c) == null);
        }

        need(actual.keySet().equals(out.keySet()));

        for (var e : out.entrySet()) {
            need(
                    FrameworkDexMaterializer.semantic(e.getValue())
                            .equals(FrameworkDexMaterializer.semantic(actual.get(e.getKey()))));
        }

        return result;
    }

    static Method sharedLibrariesMethod(ClassDef target) {
        Method found = null;

        for (Method m : target.getDirectMethods()) {
            if (m.getName().equals("modifySharedLibraries")) {
                need(found == null);
                found = m;
            }
        }

        need(
                found != null
                        && found.getAccessFlags() == 9
                        && found.getParameterTypes().toString().equals("[" + PKG + ", Z]")
                        && found.getReturnType().equals("V")
                        && found.getImplementation() != null
                        && found.getImplementation().getRegisterCount() == 3);

        return found;
    }

    static Method hookedSharedLibraries(Method found) {
        List<Instruction> ins = new ArrayList<>();

        found.getImplementation().getInstructions().forEach(ins::add);
        need(
                ins.size() == 3
                        && ins.get(0).getOpcode() == Opcode.SGET_OBJECT
                        && ins.get(1).getOpcode() == Opcode.INVOKE_VIRTUAL
                        && ins.get(2).getOpcode() == Opcode.RETURN_VOID);
        need(
                ((ReferenceInstruction) ins.get(0))
                        .getReference()
                        .toString()
                        .equals(OWNER + "->INSTANCE:" + OWNER));
        need(
                ((ReferenceInstruction) ins.get(1))
                        .getReference()
                        .toString()
                        .equals(OWNER + "->updatePackage(" + PKG + "Z)V"));
        need(((OneRegisterInstruction) ins.get(0)).getRegisterA() == 0);
        var call = (FiveRegisterInstruction) ins.get(1);
        need(
                call.getRegisterCount() == 3
                        && call.getRegisterC() == 0
                        && call.getRegisterD() == 1
                        && call.getRegisterE() == 2);
        var body = new MutableMethodImplementation(found.getImplementation());
        body.addInstruction(
                2,
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        1,
                        1,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                "Lcom/android/server/pm/parsing/library/QgLibraries;",
                                "apply",
                                List.of(PKG),
                                "V")));

        return new ImmutableMethod(
                found.getDefiningClass(),
                found.getName(),
                found.getParameters(),
                found.getReturnType(),
                found.getAccessFlags(),
                found.getAnnotations(),
                found.getHiddenApiRestrictions(),
                body);
    }
}
