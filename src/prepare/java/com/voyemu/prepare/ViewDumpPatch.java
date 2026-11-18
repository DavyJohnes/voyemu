package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.formats.Instruction35c;
import org.jf.dexlib2.iface.reference.MethodReference;
import org.jf.dexlib2.iface.reference.StringReference;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction10x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction35c;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;
import org.jf.dexlib2.writer.io.MemoryDataStore;
import org.jf.dexlib2.writer.pool.DexPool;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HexFormat;
import java.util.List;
import java.util.Set;
import java.util.TreeMap;
import java.util.zip.Adler32;

/** Pure API28 leaf. Caller must supply pinned base/helper DEX hashes from its registered recipe. */
final class ViewDumpPatch {
    static final String OWNER = "Landroid/view/ViewDebug;";
    static final String HELPER = "Lcom/voyemu/api/ViewDumpBridge;";
    static final List<String> PARAMETERS =
            List.of("Landroid/content/Context;", "Ljava/lang/Object;", "Ljava/io/BufferedWriter;");
    static final String NAME = "dumpViewProperties";

    private ViewDumpPatch() {}

    static void require(boolean value) {
        Failure.require(
                value, "VIEW_DUMP_PATCH_INVALID", "API28 ViewDebug patch contract mismatch", 5);
    }

    static String sha(byte[] bytes) throws Exception {
        return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));
    }

    static void checkedDex(byte[] bytes, String pin) throws Exception {
        require(pin != null && pin.matches("[0-9a-f]{64}") && sha(bytes).equals(pin));
        require(bytes.length >= 112 && bytes.length <= 32 * 1024 * 1024);
        require(
                Arrays.equals(
                                Arrays.copyOf(bytes, 8),
                                new byte[] {'d', 'e', 'x', '\n', '0', '3', '9', 0})
                        || Arrays.equals(
                                Arrays.copyOf(bytes, 8),
                                new byte[] {'d', 'e', 'x', '\n', '0', '3', '8', 0})
                        || Arrays.equals(
                                Arrays.copyOf(bytes, 8),
                                new byte[] {'d', 'e', 'x', '\n', '0', '3', '5', 0}));
        ByteBuffer header = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        require(
                header.getInt(32) == bytes.length
                        && header.getInt(36) == 112
                        && header.getInt(40) == 0x12345678);
        Adler32 checksum = new Adler32();
        checksum.update(bytes, 12, bytes.length - 12);
        require(header.getInt(8) == (int) checksum.getValue());
        MessageDigest signature = MessageDigest.getInstance("SHA-1");
        signature.update(bytes, 32, bytes.length - 32);
        require(Arrays.equals(signature.digest(), Arrays.copyOfRange(bytes, 12, 32)));
    }

    static boolean target(MethodReference method, String owner, List<String> parameters) {
        return method.getDefiningClass().equals(owner)
                && method.getName().equals(NAME)
                && method.getReturnType().equals("V")
                && method.getParameterTypes().equals(parameters);
    }

    private static void original(Method method) {
        int flags = method.getAccessFlags();
        int visibility = flags & 7;

        if (visibility != 0 && (visibility & (visibility - 1)) != 0) {
            flags ^= 7;
        }

        flags &= ~((flags & 0x100) != 0 ? 0x200 : 0x20);
        require(flags == 10 && method.getImplementation() != null);
        var implementation = method.getImplementation();
        require(implementation.getRegisterCount() == 4 && implementation.getTryBlocks().isEmpty());
        List<Instruction> instructions = new ArrayList<>();
        implementation.getInstructions().forEach(instructions::add);
        require(instructions.size() == 3);
        Instruction constant = instructions.get(0), call = instructions.get(1);
        require(
                (constant.getOpcode() == Opcode.CONST_STRING
                                || constant.getOpcode() == Opcode.CONST_STRING_JUMBO)
                        && constant instanceof OneRegisterInstruction oneRegisterInstruction
                        && oneRegisterInstruction.getRegisterA() == 0
                        && ((ReferenceInstruction) constant).getReference()
                                instanceof StringReference
                        && ((StringReference) ((ReferenceInstruction) constant).getReference())
                                .getString()
                                .isEmpty());
        require(call.getOpcode() == Opcode.INVOKE_STATIC && call instanceof Instruction35c);
        Instruction35c invoke = (Instruction35c) call;
        require(
                invoke.getRegisterCount() == 4
                        && invoke.getRegisterC() == 1
                        && invoke.getRegisterD() == 2
                        && invoke.getRegisterE() == 3
                        && invoke.getRegisterF() == 0
                        && invoke.getReference() instanceof MethodReference);
        List<String> recursive = new ArrayList<>(PARAMETERS);
        recursive.add("Ljava/lang/String;");
        require(target((MethodReference) invoke.getReference(), OWNER, recursive));
        require(instructions.get(2).getOpcode() == Opcode.RETURN_VOID);
    }

    static byte[] patch(byte[] source, String sourcePin, byte[] helper, String helperPin)
            throws Exception {
        return patch(source, sourcePin, helper, helperPin, 28);
    }

    static byte[] patch(byte[] source, String sourcePin, byte[] helper, String helperPin, int api)
            throws Exception {
        require(api == 28 || api == 30);
        checkedDex(source, sourcePin);
        checkedDex(helper, helperPin);
        var base = new DexBackedDexFile(Opcodes.forApi(api), source);
        var donor = new DexBackedDexFile(Opcodes.forApi(api), helper);
        TreeMap<String, ClassDef> classes = new TreeMap<>();

        for (ClassDef type : base.getClasses()) {
            require(
                    classes.put(type.getType(), api == 30 ? ImmutableClassDef.of(type) : type)
                            == null);
        }

        ClassDef owner = classes.get(OWNER);
        require(owner != null);
        int helperEntry = 0;

        for (ClassDef type : donor.getClasses()) {
            require(
                    type.getType().equals(HELPER)
                            || type.getType()
                                    .startsWith(HELPER.substring(0, HELPER.length() - 1) + "$"));
            require(classes.put(type.getType(), type) == null);

            for (Method method : type.getMethods()) {
                if (target(method, HELPER, PARAMETERS)) {
                    require(
                            (method.getAccessFlags() & 9) == 9
                                    && method.getImplementation() != null);
                    helperEntry++;
                }
            }
        }

        require(helperEntry == 1);
        List<Method> methods = new ArrayList<>();
        int replaced = 0;

        for (Method method : owner.getDirectMethods()) {
            if (!target(method, OWNER, PARAMETERS)) {
                methods.add(method);
                continue;
            }

            original(method);
            replaced++;
            var implementation =
                    new ImmutableMethodImplementation(
                            3,
                            List.of(
                                    new ImmutableInstruction35c(
                                            Opcode.INVOKE_STATIC,
                                            3,
                                            0,
                                            1,
                                            2,
                                            0,
                                            0,
                                            new ImmutableMethodReference(
                                                    HELPER, NAME, PARAMETERS, "V")),
                                    new ImmutableInstruction10x(Opcode.RETURN_VOID)),
                            List.of(),
                            List.of());
            methods.add(
                    new ImmutableMethod(
                            OWNER,
                            NAME,
                            method.getParameters(),
                            "V",
                            method.getAccessFlags(),
                            method.getAnnotations(),
                            method.getHiddenApiRestrictions(),
                            implementation));
        }

        require(replaced == 1);
        classes.put(
                OWNER,
                new ImmutableClassDef(
                        OWNER,
                        owner.getAccessFlags(),
                        owner.getSuperclass(),
                        owner.getInterfaces(),
                        owner.getSourceFile(),
                        owner.getAnnotations(),
                        owner.getStaticFields(),
                        owner.getInstanceFields(),
                        methods,
                        owner.getVirtualMethods()));
        DexPool pool = new DexPool(Opcodes.forApi(api));

        for (ClassDef type : classes.values()) {
            pool.internClass(type);

            // Pinned dexlib2 2.5.2 omits invoke-polymorphic reference2 interning.
            for (Method method : type.getMethods()) {
                if (method.getImplementation() != null) {
                    for (Instruction instruction : method.getImplementation().getInstructions()) {
                        if (instruction
                                instanceof
                                org.jf.dexlib2.iface.instruction.DualReferenceInstruction
                                        dualReferenceInstruction) {
                            var reference = dualReferenceInstruction.getReference2();
                            require(
                                    reference
                                            instanceof
                                            org.jf.dexlib2.iface.reference.MethodProtoReference);
                            pool.protoSection.intern(
                                    (org.jf.dexlib2.iface.reference.MethodProtoReference)
                                            reference);
                        }
                    }
                }
            }
        }

        require(!pool.hasOverflowed());
        MemoryDataStore output = new MemoryDataStore();
        pool.writeTo(output);
        byte[] result = output.getData();
        checkedDex(result, sha(result));
        Set<String> expected = classes.keySet();
        TreeMap<String, ClassDef> actual = new TreeMap<>();

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(api), result).getClasses()) {
            require(actual.put(type.getType(), type) == null);
        }

        require(actual.keySet().equals(expected));

        return result;
    }
}
