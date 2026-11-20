package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Field;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;
import org.jf.dexlib2.immutable.reference.ImmutableTypeReference;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

final class OriginalNaviEglGuard {
    static final String OWNER = "Ljavax/microedition/khronos/egl/EGLContext;",
            TYPE = "Ljavax/microedition/khronos/egl/EGL;";

    static void need(boolean b) {
        Failure.require(b, "ORIGINAL_NAVI_EGL", "Exact original Navi EGL contract required", 3);
    }

    static byte[] apply(String profile, int api, byte[] b) throws Exception {
        need(api == 30 && b != null);
        var dex = new DexBackedDexFile(Opcodes.forApi(30), b);
        Map<String, ClassDef> old = new TreeMap<>(), out = new TreeMap<>();

        for (ClassDef c : dex.getClasses()) {
            need(old.put(c.getType(), c) == null);
        }

        ClassDef target = old.get(OWNER);
        need(target != null && target.getAccessFlags() == 1025);
        Method found = eglGetter(target);
        Method changed = hookedGetter(found);

        for (ClassDef c : old.values()) {
            if (!c.getType().equals(OWNER)) {
                out.put(c.getType(), ImmutableClassDef.of(c));
                continue;
            }

            List<Method> direct = new ArrayList<>(), virt = new ArrayList<>();

            for (Method m : c.getDirectMethods()) {
                direct.add(m.getName().equals("getEGL") ? changed : m);
            }

            for (Method m : c.getVirtualMethods()) {
                virt.add(m);
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
                            virt));
        }

        byte[] result =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), out.values()));
        result = HiddenApiOffsets.fix(b, result);
        var reread = new DexBackedDexFile(Opcodes.forApi(30), result);
        Map<String, ClassDef> actual = new TreeMap<>();

        for (ClassDef c : reread.getClasses()) {
            actual.put(c.getType(), c);
        }

        need(actual.keySet().equals(out.keySet()));

        for (var e : out.entrySet()) {
            if (!FrameworkDexMaterializer.semantic(e.getValue())
                    .equals(FrameworkDexMaterializer.semantic(actual.get(e.getKey())))) {
                throw new Failure(
                        "ORIGINAL_NAVI_EGL", "Unrelated DEX semantics changed: " + e.getKey(), 3);
            }
        }

        return result;
    }

    static Method eglGetter(ClassDef target) {
        int fields = 0;

        for (Field f : target.getFields()) {
            if (f.getName().equals("EGL_INSTANCE")) {
                need(f.getAccessFlags() == 26 && f.getType().equals(TYPE));
                fields++;
            }
        }

        need(fields == 1);
        Method found = null;

        for (Method m : target.getMethods()) {
            if (m.getName().equals("getEGL")) {
                need(found == null);
                found = m;
            }
        }

        need(
                found != null
                        && found.getAccessFlags() == 9
                        && found.getParameters().isEmpty()
                        && found.getReturnType().equals(TYPE)
                        && found.getImplementation() != null
                        && found.getImplementation().getRegisterCount() == 1);

        return found;
    }

    static Method hookedGetter(Method found) {
        List<Instruction> ins = new ArrayList<>();

        found.getImplementation().getInstructions().forEach(ins::add);
        need(
                ins.size() == 2
                        && ins.get(0).getOpcode() == Opcode.SGET_OBJECT
                        && ins.get(1).getOpcode() == Opcode.RETURN_OBJECT
                        && ((OneRegisterInstruction) ins.get(0)).getRegisterA() == 0
                        && ((OneRegisterInstruction) ins.get(1)).getRegisterA() == 0);
        var ref = ((ReferenceInstruction) ins.get(0)).getReference();
        need(ref.toString().equals(OWNER + "->EGL_INSTANCE:" + TYPE));
        var body = new MutableMethodImplementation(found.getImplementation());
        body.addInstruction(
                1,
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        1,
                        0,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                "Lcom/voyemu/egl/ScopedEglHook;",
                                "get",
                                List.of("Ljava/lang/Object;"),
                                "Ljava/lang/Object;")));
        body.addInstruction(2, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0));
        body.addInstruction(
                3,
                new BuilderInstruction21c(Opcode.CHECK_CAST, 0, new ImmutableTypeReference(TYPE)));

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
