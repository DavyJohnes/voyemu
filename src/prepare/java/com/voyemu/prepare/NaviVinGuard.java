package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.ExceptionHandler;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.ImmutableTryBlock;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/** Propagate genuine OEM VIN only when the existing activation input is empty. */
final class NaviVinGuard {
    static final String NAME = "api30-navi-vin",
            PIN = NaviVehicleGuard.PIN,
            OWNER = "Lcom/baidu/navisdk/integration/controller/carinfo/CarInfoProvider;",
            HELPER = "Lcom/voyemu/navi/GenuineVin;";
    static final String HELPER_ROOT = "api30-navi-vin/src",
            HELPER_PATH = HELPER_ROOT + "/com/voyemu/navi/GenuineVin.java";

    static void need(boolean v) {
        Failure.require(v, "NAVI_VIN", "Exact Free A11 7.3.1 genuine VIN boundary required", 3);
    }

    static Map<String, ClassDef> classes(byte[] b) {
        var out = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), b).getClasses()) {
            need(out.put(c.getType(), c) == null);
        }

        return out;
    }

    static Method method(Map<String, ClassDef> cs) {
        Method found = null;
        var c = cs.get(OWNER);

        need(c != null);

        for (var m : c.getMethods()) {
            if (m.getName().equals("getContent")) {
                need(found == null);
                found = m;
            }
        }

        need(found != null);

        return found;
    }

    static void validate(String profile, int api, String abi, Path apk, byte[] raw)
            throws Exception {
        need(
                api == 30
                        && "arm64-v8a".equals(abi)
                        && PIN.equals(FilesSupport.hash(apk))
                        && raw != null);
        method(classes(raw));
    }

    // DexPool can coalesce adjacent protected ranges with identical ordered handlers.
    // Canonicalize only that representation; every instruction, handler, debug item and other
    // metadata remains compared.
    static Method canonical(Method m) throws Exception {
        var impl = m.getImplementation();

        if (impl == null) {
            return m;
        }

        var tries = new ArrayList<org.jf.dexlib2.iface.TryBlock<? extends ExceptionHandler>>();

        for (var t : impl.getTryBlocks()) {
            if (!tries.isEmpty()) {
                var last = tries.get(tries.size() - 1);

                if (last.getStartCodeAddress() + last.getCodeUnitCount() == t.getStartCodeAddress()
                        && FrameworkDexMaterializer.semantic(last.getExceptionHandlers())
                                .equals(
                                        FrameworkDexMaterializer.semantic(
                                                t.getExceptionHandlers()))) {
                    tries.set(
                            tries.size() - 1,
                            new ImmutableTryBlock(
                                    last.getStartCodeAddress(),
                                    last.getCodeUnitCount() + t.getCodeUnitCount(),
                                    last.getExceptionHandlers()));
                    continue;
                }
            }

            tries.add(t);
        }

        var code =
                new ImmutableMethodImplementation(
                        impl.getRegisterCount(),
                        impl.getInstructions(),
                        tries,
                        impl.getDebugItems());

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

    static ClassDef canonical(ClassDef c) throws Exception {
        var direct = new ArrayList<Method>();
        var virtual = new ArrayList<Method>();

        for (var m : c.getDirectMethods()) {
            direct.add(canonical(m));
        }

        for (var m : c.getVirtualMethods()) {
            virtual.add(canonical(m));
        }

        return new ImmutableClassDef(
                c.getType(),
                c.getAccessFlags(),
                c.getSuperclass(),
                c.getInterfaces(),
                c.getSourceFile(),
                c.getAnnotations(),
                c.getStaticFields(),
                c.getInstanceFields(),
                direct,
                virtual);
    }

    static boolean same(ClassDef a, ClassDef b) throws Exception {
        return FrameworkDexMaterializer.semantic(a).equals(FrameworkDexMaterializer.semantic(b))
                || FrameworkDexMaterializer.semantic(canonical(a))
                        .equals(FrameworkDexMaterializer.semantic(canonical(b)));
    }

    static byte[] apply(String profile, int api, String abi, Path apk, byte[] raw)
            throws Exception {
        validate(profile, api, abi, apk, raw);
        var before = classes(raw);
        var m = method(before);
        var b = new MutableMethodImplementation(m.getImplementation());
        int at = -1, returns = 0;

        for (int i = 0; i < b.getInstructions().size(); i++) {
            if (b.getInstructions().get(i).getOpcode() == Opcode.RETURN_OBJECT) {
                returns++;
                at = i;
            }
        }

        need(
                returns == 2
                        && at >= 0
                        && m.getImplementation().getRegisterCount() == 4
                        && ((OneRegisterInstruction) b.getInstructions().get(at)).getRegisterA()
                                == 1);
        b.replaceInstruction(
                at,
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        2,
                        3,
                        1,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                HELPER,
                                "selectContent",
                                List.of("Ljava/lang/String;", "Ljava/lang/Object;"),
                                "Ljava/lang/Object;")));
        b.addInstruction(at + 1, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 1));
        b.addInstruction(at + 2, new BuilderInstruction11x(Opcode.RETURN_OBJECT, 1));
        var changed =
                new ImmutableMethod(
                        m.getDefiningClass(),
                        m.getName(),
                        m.getParameters(),
                        m.getReturnType(),
                        m.getAccessFlags(),
                        m.getAnnotations(),
                        m.getHiddenApiRestrictions(),
                        b);
        var out = new TreeMap<String, ClassDef>();

        for (var c : before.values()) {
            if (!c.getType().equals(OWNER)) {
                out.put(c.getType(), c);
                continue;
            }

            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();

            for (var x : c.getDirectMethods()) {
                direct.add(x.getName().equals(m.getName()) ? changed : x);
            }

            for (var x : c.getVirtualMethods()) {
                virtual.add(x.getName().equals(m.getName()) ? changed : x);
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
        need(before.keySet().equals(reread.keySet()));

        for (var e : out.entrySet()) {
            need(same(e.getValue(), reread.get(e.getKey())));
        }

        return result;
    }
}
