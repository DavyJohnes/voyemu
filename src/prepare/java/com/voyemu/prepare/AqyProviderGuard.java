package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11n;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;
import org.jf.dexlib2.immutable.reference.ImmutableStringReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.zip.ZipFile;

// Keep the initialized OEM video view; an absent optional app cannot provide a content observer.
final class AqyProviderGuard {
    static final String NAME = "api30-aqy-provider";

    static final String OWNER = "Lcom/qinggan/app/aiqiyi/present/MainRankPresent;",
            AUTHORITY = "com.qinggan.app.aiqiyi.data.AQYContentProvider";

    /** True when the Launcher DEX carries the optional OEM video class this patch guards. */
    static boolean present(byte[] dex) {
        if (dex == null) {
            return false;
        }

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            if (type.getType().equals(OWNER)) {
                return true;
            }
        }

        return false;
    }

    private static void require(boolean value) {
        Failure.require(
                value, "AQY_PROVIDER_CONTRACT", "Exact optional AQY provider boundary required", 3);
    }

    private static boolean target(Method method) {
        return method.getName().equals("<init>")
                && method.getParameterTypes().equals(List.of("Landroid/content/Context;"))
                && method.getReturnType().equals("V");
    }

    static Method method(byte[] dex) {
        Method found = null;

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            if (type.getType().equals(OWNER)) {
                for (Method candidate : type.getMethods()) {
                    if (target(candidate)) {
                        require(found == null);
                        found = candidate;
                    }
                }
            }
        }

        require(
                found != null
                        && found.getAccessFlags() == 65537
                        && found.getImplementation() != null
                        && found.getImplementation().getRegisterCount() == 7
                        && found.getImplementation().getTryBlocks().isEmpty());

        return found;
    }

    static void validate(Map<String, Object> constants, int api, Path original, byte[] dex)
            throws Exception {
        require(api == 30 && StepConstants.admits(constants, FilesSupport.hash(original)));
        require(dex != null);
        Method selected = method(dex);

        if (StepConstants.flag(constants, "aqy_boundary")) {
            boundary(selected);
        }
    }

    private static void boundary(Method m) {
        int address = 0, seen = 0;

        for (var i : m.getImplementation().getInstructions()) {
            if (address == 0xd4 || address == 0xda) {
                require(
                        i.getOpcode() == Opcode.INVOKE_VIRTUAL
                                && i
                                        instanceof
                                        org.jf.dexlib2.iface.instruction.ReferenceInstruction
                                && i
                                        instanceof
                                        org.jf.dexlib2.iface.instruction.FiveRegisterInstruction);
                var r = (org.jf.dexlib2.iface.instruction.ReferenceInstruction) i;
                var regs = (org.jf.dexlib2.iface.instruction.FiveRegisterInstruction) i;

                if (address == 0xd4) {
                    require(
                            r.getReference()
                                            .toString()
                                            .equals(
                                                    "Landroid/content/Context;->getContentResolver()Landroid/content/ContentResolver;")
                                    && regs.getRegisterCount() == 1
                                    && regs.getRegisterC() == 6);
                } else {
                    require(
                            r.getReference()
                                            .toString()
                                            .equals(
                                                    "Landroid/content/ContentResolver;->registerContentObserver(Landroid/net/Uri;ZLandroid/database/ContentObserver;)V")
                                    && regs.getRegisterCount() == 4
                                    && regs.getRegisterC() == 6
                                    && regs.getRegisterD() == 1
                                    && regs.getRegisterE() == 0
                                    && regs.getRegisterF() == 2);
                }

                seen++;
            }

            if (address == 0xdd) {
                require(i.getOpcode() == Opcode.RETURN_VOID);
                seen++;
            }

            address += i.getCodeUnits();
        }

        require(seen == 3);
    }

    private static Method guarded(Method method) {
        var body = new MutableMethodImplementation(method.getImplementation());
        var absent = body.newLabelForAddress(0xdd);
        int address = 0, index = -1;

        for (int i = 0; i < body.getInstructions().size(); i++) {
            if (address == 0xd4) {
                index = i;
            }

            address += body.getInstructions().get(i).getCodeUnits();
        }

        require(index >= 0);
        body.addInstruction(
                index++,
                new BuilderInstruction35c(
                        Opcode.INVOKE_VIRTUAL,
                        1,
                        6,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                "Landroid/content/Context;",
                                "getPackageManager",
                                List.of(),
                                "Landroid/content/pm/PackageManager;")));
        body.addInstruction(index++, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 3));
        body.addInstruction(
                index++,
                new BuilderInstruction21c(
                        Opcode.CONST_STRING, 4, new ImmutableStringReference(AUTHORITY)));
        body.addInstruction(index++, new BuilderInstruction11n(Opcode.CONST_4, 2, 0));
        body.addInstruction(
                index++,
                new BuilderInstruction35c(
                        Opcode.INVOKE_VIRTUAL,
                        3,
                        3,
                        4,
                        2,
                        0,
                        0,
                        new ImmutableMethodReference(
                                "Landroid/content/pm/PackageManager;",
                                "resolveContentProvider",
                                List.of("Ljava/lang/String;", "I"),
                                "Landroid/content/pm/ProviderInfo;")));
        body.addInstruction(index++, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 3));
        body.addInstruction(index, new BuilderInstruction21t(Opcode.IF_EQZ, 3, absent));

        return new ImmutableMethod(
                method.getDefiningClass(),
                method.getName(),
                method.getParameters(),
                method.getReturnType(),
                method.getAccessFlags(),
                method.getAnnotations(),
                method.getHiddenApiRestrictions(),
                body);
    }

    static byte[] validateOriginal(Map<String, Object> constants, int api, Path original)
            throws Exception {
        byte[] source;

        try (var zip = new ZipFile(original.toFile())) {
            var entry = zip.getEntry("classes2.dex");
            require(entry != null && entry.getSize() > 0 && entry.getSize() <= 32 * 1024 * 1024);
            source = zip.getInputStream(entry).readAllBytes();
        }

        validate(constants, api, original, source);

        return source;
    }

    static byte[] apply(Map<String, Object> constants, int api, Path original, byte[] current)
            throws Exception {
        byte[] source = validateOriginal(constants, api, original);

        require(current != null);

        if (StepConstants.flag(constants, "aqy_boundary")) {
            require(
                    FrameworkDexMaterializer.semantic(method(current))
                            .equals(FrameworkDexMaterializer.semantic(method(source))));
            boundary(method(current));
        } else {
            method(current);
        }

        var classes = new TreeMap<String, ClassDef>();

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(30), current).getClasses()) {
            require(classes.put(type.getType(), type) == null);
        }

        ClassDef selected = classes.get(OWNER);
        require(selected != null);
        var direct = new ArrayList<Method>();
        int count = 0;

        for (Method method : selected.getDirectMethods()) {
            if (target(method)) {
                direct.add(guarded(method));
                count++;
            } else {
                direct.add(method);
            }
        }

        require(count == 1);
        classes.put(
                OWNER,
                new ImmutableClassDef(
                        selected.getType(),
                        selected.getAccessFlags(),
                        selected.getSuperclass(),
                        selected.getInterfaces(),
                        selected.getSourceFile(),
                        selected.getAnnotations(),
                        selected.getStaticFields(),
                        selected.getInstanceFields(),
                        direct,
                        selected.getVirtualMethods()));
        byte[] output =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), classes.values()));
        var reread = new TreeMap<String, ClassDef>();

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(30), output).getClasses()) {
            require(reread.put(type.getType(), type) == null);
        }

        require(reread.keySet().equals(classes.keySet()));

        for (var definition : classes.entrySet()) {
            require(
                    FrameworkDexMaterializer.semantic(definition.getValue())
                            .equals(
                                    FrameworkDexMaterializer.semantic(
                                            reread.get(definition.getKey()))));
        }

        require(StepConstants.admits(constants, FilesSupport.hash(original)));

        return output;
    }
}
