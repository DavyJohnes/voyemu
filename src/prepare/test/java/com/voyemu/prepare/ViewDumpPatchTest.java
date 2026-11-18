package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.reference.MethodReference;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.ImmutableMethodParameter;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction10x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction21c;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction35c;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;
import org.jf.dexlib2.immutable.reference.ImmutableStringReference;
import org.jf.dexlib2.writer.io.MemoryDataStore;
import org.jf.dexlib2.writer.pool.DexPool;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Set;

public final class ViewDumpPatchTest {
    static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    static ImmutableMethod method(
            String owner, List<String> types, int flags, int registers, List<Instruction> code) {
        List<ImmutableMethodParameter> parameters = new ArrayList<>();

        for (String type : types) {
            parameters.add(new ImmutableMethodParameter(type, Set.of(), null));
        }

        return new ImmutableMethod(
                owner,
                ViewDumpPatch.NAME,
                parameters,
                "V",
                flags,
                Set.of(),
                Set.of(),
                new ImmutableMethodImplementation(registers, code, List.of(), List.of()));
    }

    static ImmutableClassDef type(String owner, List<? extends Method> methods) {
        return new ImmutableClassDef(
                owner, 1, "Ljava/lang/Object;", List.of(), null, Set.of(), List.of(), methods);
    }

    static byte[] dex(List<? extends ClassDef> classes) throws Exception {
        DexPool pool = new DexPool(Opcodes.forApi(28));

        for (ClassDef type : classes) {
            pool.internClass(type);
        }

        MemoryDataStore output = new MemoryDataStore();
        pool.writeTo(output);

        return output.getData();
    }

    static byte[] source(int flags, int registers, String prefix) throws Exception {
        List<String> recursive = new ArrayList<>(ViewDumpPatch.PARAMETERS);

        recursive.add("Ljava/lang/String;");
        var wrapper =
                method(
                        ViewDumpPatch.OWNER,
                        ViewDumpPatch.PARAMETERS,
                        flags,
                        registers,
                        List.of(
                                new ImmutableInstruction21c(
                                        Opcode.CONST_STRING,
                                        0,
                                        new ImmutableStringReference(prefix)),
                                new ImmutableInstruction35c(
                                        Opcode.INVOKE_STATIC,
                                        4,
                                        1,
                                        2,
                                        3,
                                        0,
                                        0,
                                        new ImmutableMethodReference(
                                                ViewDumpPatch.OWNER,
                                                ViewDumpPatch.NAME,
                                                recursive,
                                                "V")),
                                new ImmutableInstruction10x(Opcode.RETURN_VOID)));
        var original =
                method(
                        ViewDumpPatch.OWNER,
                        recursive,
                        10,
                        4,
                        List.of(new ImmutableInstruction10x(Opcode.RETURN_VOID)));
        var unrelated =
                method(
                        "Lfixture/Unchanged;",
                        List.of(),
                        9,
                        0,
                        List.of(new ImmutableInstruction10x(Opcode.RETURN_VOID)));
        var encodedPrivate =
                method(
                        ViewDumpPatch.OWNER,
                        List.of(),
                        37,
                        1,
                        List.of(new ImmutableInstruction10x(Opcode.RETURN_VOID)));

        return dex(
                List.of(
                        new ImmutableClassDef(
                                ViewDumpPatch.OWNER,
                                1,
                                "Ljava/lang/Object;",
                                List.of(),
                                null,
                                Set.of(),
                                List.of(),
                                List.of(),
                                List.of(wrapper, original, encodedPrivate),
                                List.of()),
                        type("Lfixture/Unchanged;", List.of(unrelated))));
    }

    static byte[] helper(String owner) throws Exception {
        return dex(
                List.of(
                        type(
                                owner,
                                List.of(
                                        method(
                                                owner,
                                                ViewDumpPatch.PARAMETERS,
                                                9,
                                                3,
                                                List.of(
                                                        new ImmutableInstruction10x(
                                                                Opcode.RETURN_VOID)))))));
    }

    static void refused(byte[] source, String sourcePin, byte[] helper, String helperPin)
            throws Exception {
        try {
            ViewDumpPatch.patch(source, sourcePin, helper, helperPin);
            throw new AssertionError("patch accepted");
        } catch (Failure expected) {
            check(expected.code.equals("VIEW_DUMP_PATCH_INVALID"));
        }
    }

    static boolean publicAccess(int flags) {
        int visibility = flags & 7;

        if (visibility != 0 && (visibility & (visibility - 1)) != 0) {
            flags ^= 7;
        }

        return (flags & 1) != 0;
    }

    static void checkRedirect(Method m) {
        check(m.getAccessFlags() == 42 && m.getImplementation().getRegisterCount() == 3);
        List<Instruction> code = new ArrayList<>();
        m.getImplementation().getInstructions().forEach(code::add);
        check(
                code.size() == 2
                        && code.get(0).getOpcode() == Opcode.INVOKE_STATIC
                        && code.get(1).getOpcode() == Opcode.RETURN_VOID);
        check(
                ViewDumpPatch.target(
                        (MethodReference) ((ReferenceInstruction) code.get(0)).getReference(),
                        ViewDumpPatch.HELPER,
                        ViewDumpPatch.PARAMETERS));
    }

    static java.util.Map<String, ClassDef> pinnedAndroid(Path canonical) throws Exception {
        java.util.Map<String, ClassDef> android = new java.util.TreeMap<>();
        String[] names = {"classes.dex", "classes2.dex", "classes3.dex"};
        String[] pins = {
            "5008b3c9f23fb6ebed3facf657ac4618fe392d68037d12e6e6566b239bdab26e",
            "d538f4e8b05689d4e445fc5f34fe39419c06b75f87de94d308cab3bb28e45248",
            "5c3945ed0c110bae0062dc7fc4787441267960ab4a3e51a6ff6a766dab96fab3"
        };

        for (int i = 0; i < names.length; i++) {
            byte[] bytes = Files.readAllBytes(canonical.resolve(names[i]));
            ViewDumpPatch.checkedDex(bytes, pins[i]);

            for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(28), bytes).getClasses()) {
                android.put(type.getType(), type);
            }
        }

        return android;
    }

    static boolean methodResolves(java.util.Map<String, ClassDef> android, MethodReference target) {
        ClassDef owner = android.get(target.getDefiningClass());
        boolean found = false;

        while (owner != null && !found) {
            for (Method member : owner.getMethods()) {
                if (member.getName().equals(target.getName())
                        && member.getParameterTypes().equals(target.getParameterTypes())
                        && member.getReturnType().equals(target.getReturnType())
                        && publicAccess(member.getAccessFlags())) {
                    found = true;
                }
            }

            owner = android.get(owner.getSuperclass());
        }

        return found;
    }

    static boolean fieldResolves(
            java.util.Map<String, ClassDef> android,
            org.jf.dexlib2.iface.reference.FieldReference target) {
        ClassDef owner = android.get(target.getDefiningClass());
        boolean found = false;

        while (owner != null && !found) {
            for (var member : owner.getFields()) {
                if (member.getName().equals(target.getName())
                        && member.getType().equals(target.getType())
                        && publicAccess(member.getAccessFlags())) {
                    found = true;
                }
            }

            owner = android.get(owner.getSuperclass());
        }

        return found;
    }

    static void checkAndroidSurface(java.util.Map<String, ClassDef> android, byte[] helper) {
        int androidMethods = 0, androidFields = 0;

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(28), helper).getClasses()) {
            for (Method method : type.getMethods()) {
                if (method.getImplementation() != null) {
                    for (Instruction instruction : method.getImplementation().getInstructions()) {
                        if (instruction instanceof ReferenceInstruction referenceInstruction) {
                            var reference = referenceInstruction.getReference();

                            if (reference instanceof MethodReference methodReference
                                    && methodReference.getDefiningClass().startsWith("Landroid/")) {
                                check(methodResolves(android, methodReference));
                                androidMethods++;
                            }

                            if (reference
                                            instanceof
                                            org.jf.dexlib2.iface.reference.FieldReference
                                                    fieldReference
                                    && fieldReference.getDefiningClass().startsWith("Landroid/")) {
                                check(fieldResolves(android, fieldReference));
                                androidFields++;
                            }
                        }
                    }
                }
            }
        }

        System.out.println(
                "Actual API28 surface: "
                        + androidMethods
                        + " Android method references / "
                        + androidFields
                        + " Android field references all resolve in pinned base framework");
    }

    static ClassDef recoveredOwner(ClassDef original, ClassDef current, int[] targets) {
        Method originalTarget = null;

        for (Method m : original.getDirectMethods()) {
            if (ViewDumpPatch.target(m, ViewDumpPatch.OWNER, ViewDumpPatch.PARAMETERS)) {
                originalTarget = m;
            }
        }

        check(originalTarget != null && originalTarget.getAccessFlags() == 42);
        List<Method> recovered = new ArrayList<>();

        for (Method m : current.getDirectMethods()) {
            if (ViewDumpPatch.target(m, ViewDumpPatch.OWNER, ViewDumpPatch.PARAMETERS)) {
                checkRedirect(m);
                targets[0]++;
                recovered.add(originalTarget);
            } else {
                recovered.add(m);
            }
        }

        return new ImmutableClassDef(
                current.getType(),
                current.getAccessFlags(),
                current.getSuperclass(),
                current.getInterfaces(),
                current.getSourceFile(),
                current.getAnnotations(),
                current.getStaticFields(),
                current.getInstanceFields(),
                recovered,
                current.getVirtualMethods());
    }

    static void actual(String[] args) throws Exception {
        byte[] source = Files.readAllBytes(Path.of(args[0])),
                helper = Files.readAllBytes(Path.of(args[2]));
        // Public Android surface compatibility against all three caller-pinned actual API28 DEX.
        java.util.Map<String, ClassDef> android = pinnedAndroid(Path.of(args[0]).getParent());

        checkAndroidSurface(android, helper);
        byte[] result = ViewDumpPatch.patch(source, args[1], helper, args[3]);
        var base = new DexBackedDexFile(Opcodes.forApi(28), source);
        var patched = new DexBackedDexFile(Opcodes.forApi(28), result);
        java.util.Map<String, ClassDef> before = new java.util.TreeMap<>(),
                after = new java.util.TreeMap<>();

        for (ClassDef type : base.getClasses()) {
            before.put(type.getType(), type);
        }

        for (ClassDef type : patched.getClasses()) {
            after.put(type.getType(), type);
        }

        int classes = 0, methods = 0;
        int[] targets = {0};

        for (var entry : before.entrySet()) {
            ClassDef original = entry.getValue(), current = after.get(entry.getKey());
            check(current != null);

            if (entry.getKey().equals(ViewDumpPatch.OWNER)) {
                current = recoveredOwner(original, current, targets);
            }

            check(
                    FrameworkDexMaterializer.semantic(original)
                            .equals(FrameworkDexMaterializer.semantic(current)));
            classes++;

            for (Method ignored : original.getMethods()) {
                methods++;
            }
        }

        check(targets[0] == 1);
        java.util.Set<String> expected = new java.util.TreeSet<>(before.keySet());

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(28), helper).getClasses()) {
            check(expected.add(type.getType()));
        }

        check(after.keySet().equals(expected));

        if (args.length == 5) {
            Files.write(Path.of(args[4]), result);
        }

        System.out.println(
                "Actual canonical framework classes2: "
                        + classes
                        + " classes / "
                        + methods
                        + " methods; raw target42 retained; all other flags, four partitions/order"
                        + " and complete code/debug/annotation semantics unchanged; patched_sha256="
                        + ViewDumpPatch.sha(result));
    }

    /** Adds the owner with original target bodies to recovered and returns its redirect count. */
    static int recoverOwner(ClassDef type, ClassDef originalOwner, List<ClassDef> recovered) {
        int calls = 0;

        for (Method method : type.getMethods()) {
            if (ViewDumpPatch.target(method, ViewDumpPatch.OWNER, ViewDumpPatch.PARAMETERS)) {
                checkRedirect(method);
                calls++;
            }
        }

        List<Method> methods = new ArrayList<>();

        for (Method method : type.getDirectMethods()) {
            if (!ViewDumpPatch.target(method, ViewDumpPatch.OWNER, ViewDumpPatch.PARAMETERS)) {
                methods.add(method);
            }
        }

        for (Method method : originalOwner.getMethods()) {
            if (ViewDumpPatch.target(method, ViewDumpPatch.OWNER, ViewDumpPatch.PARAMETERS)) {
                methods.add(method);
            }
        }

        recovered.add(
                new ImmutableClassDef(
                        type.getType(),
                        type.getAccessFlags(),
                        type.getSuperclass(),
                        type.getInterfaces(),
                        type.getSourceFile(),
                        type.getAnnotations(),
                        type.getStaticFields(),
                        type.getInstanceFields(),
                        methods,
                        type.getVirtualMethods()));

        return calls;
    }

    public static void main(String[] args) throws Exception {
        byte[] source = source(42, 4, ""), helper = helper(ViewDumpPatch.HELPER);
        String sourcePin = ViewDumpPatch.sha(source), helperPin = ViewDumpPatch.sha(helper);
        byte[] result = ViewDumpPatch.patch(source, sourcePin, helper, helperPin);

        check(!Arrays.equals(result, source));
        check(Arrays.equals(result, ViewDumpPatch.patch(source, sourcePin, helper, helperPin)));
        int classes = 0, calls = 0;
        List<ClassDef> recovered = new ArrayList<>();
        ClassDef originalOwner = null;

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(28), source).getClasses()) {
            if (type.getType().equals(ViewDumpPatch.OWNER)) {
                originalOwner = type;
            }
        }

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(28), result).getClasses()) {
            classes++;

            if (type.getType().equals(ViewDumpPatch.HELPER)) {
                continue;
            }

            if (type.getType().equals(ViewDumpPatch.OWNER)) {
                calls += recoverOwner(type, originalOwner, recovered);
            } else {
                recovered.add(type);
            }
        }

        check(classes == 3 && calls == 1);
        check(Arrays.equals(source, dex(recovered)));
        refused(source, "0".repeat(64), helper, helperPin);
        byte[] corrupt = source.clone();
        corrupt[8] ^= 1;
        refused(corrupt, ViewDumpPatch.sha(corrupt), helper, helperPin);
        refused(source, sourcePin, helper, "0".repeat(64));

        for (byte[] bad :
                new byte[][] {source(9, 4, ""), source(10, 5, ""), source(10, 4, "wrong")}) {
            refused(bad, ViewDumpPatch.sha(bad), helper, helperPin);
        }

        byte[] wrong = helper("Lfixture/Alien;");
        refused(source, sourcePin, wrong, ViewDumpPatch.sha(wrong));
        refused(result, ViewDumpPatch.sha(result), helper, helperPin);

        if (args.length == 1) {
            byte[] realHelper = Files.readAllBytes(Path.of(args[0]));
            ViewDumpPatch.patch(source, sourcePin, realHelper, ViewDumpPatch.sha(realHelper));
        }

        if (args.length == 4 || args.length == 5) {
            actual(args);
        }

        System.out.println(
                "ViewDumpPatch exact descriptor/register/checksum/hidden-flags/member-partition"
                        + " fixture cases passed");
    }
}
