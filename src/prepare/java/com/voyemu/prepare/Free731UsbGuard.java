package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.MethodImplementation;
import org.jf.dexlib2.iface.debug.DebugItem;
import org.jf.dexlib2.iface.debug.EndLocal;
import org.jf.dexlib2.iface.debug.RestartLocal;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.formats.Instruction35c;
import org.jf.dexlib2.iface.instruction.formats.Instruction3rc;
import org.jf.dexlib2.iface.reference.MethodReference;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.debug.ImmutableEndLocal;
import org.jf.dexlib2.immutable.debug.ImmutableRestartLocal;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

final class Free731UsbGuard {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1";
    static final String QGAPI_PIN =
            "c178682be164be4872697dbb5accdc6ea28446941b6d681ddb736f2fdb627c07";
    private static final String SDK_OWNER = "Lcom/android/server/StorageManagerService;";
    private static final String OEM_OWNER = "Lcom/qinggan/os/ExternalDevicesState;";
    private static final String VOLUME = "Landroid/os/storage/VolumeInfo;";
    private static final String DISK = "Landroid/os/storage/DiskInfo;";
    private static final String OEM_HELPER = "Lcom/voyemu/qgapi/VoyemuUsbPort;";
    private static final String SDK_HELPER = "Lcom/voyemu/storage/UsbMountVisibility;";
    private static final String STRING = "Ljava/lang/String;";
    private static final String CONTEXT = "Landroid/content/Context;";

    private Free731UsbGuard() {}

    private record Anchor(ClassDef owner, Method method, MutableMethodImplementation code, int index) {}

    static void checkInput(byte[] raw, boolean visibility) {
        anchor(classes(raw), visibility);
    }

    private static Anchor anchor(Map<String, ClassDef> dex, boolean visibility) {
        var owner = dex.get(visibility ? SDK_OWNER : OEM_OWNER);
        check(owner != null && !dex.containsKey(visibility ? SDK_HELPER : OEM_HELPER), "framework owner");
        Method found = null;
        for (var method : owner.getDirectMethods()) {
            boolean match = visibility
                    ? method.getName().equals("onVolumeCreatedLocked")
                            && method.getParameterTypes().equals(List.of(VOLUME))
                            && method.getReturnType().equals("V")
                    : method.getName().equals("getVolumeList")
                            && method.getParameterTypes().equals(List.of(CONTEXT))
                            && method.getReturnType().equals("Ljava/util/List;");
            if (match) {
                check(found == null && method.getImplementation() != null
                        && method.getAccessFlags() == (visibility ? 2 : 9)
                        && method.getImplementation().getRegisterCount() == (visibility ? 12 : 23),
                        "unique framework method");
                found = method;
            }
        }
        check(found != null, "framework method missing");

        var code = new MutableMethodImplementation(found.getImplementation());
        int target = -1;
        int address = 0;
        for (int i = 0; i < code.getInstructions().size(); i++) {
            var insn = code.getInstructions().get(i);
            if (visibility && insn.getOpcode() == Opcode.INVOKE_VIRTUAL
                    && ((ReferenceInstruction) insn).getReference().toString().equals(DISK + "->isAdoptable()Z")) {
                var invoke = (Instruction35c) insn;
                check(target == -1 && address == 0x120 && invoke.getRegisterCount() == 1
                        && invoke.getRegisterC() == 1 && i + 2 < code.getInstructions().size(),
                        "exact vold visibility anchor");
                var result = code.getInstructions().get(i + 1);
                var branch = code.getInstructions().get(i + 2);
                check(result.getOpcode() == Opcode.MOVE_RESULT
                        && ((OneRegisterInstruction) result).getRegisterA() == 1
                        && branch.getOpcode() == Opcode.IF_EQZ
                        && ((OneRegisterInstruction) branch).getRegisterA() == 1
                        && ((OffsetInstruction) branch).getCodeOffset() == 7, "exact vold visibility branch");
                target = i + 2;
            } else if (!visibility && insn.getOpcode() == Opcode.INVOKE_STATIC_RANGE
                    && ((ReferenceInstruction) insn).getReference() instanceof MethodReference ref
                    && ref.getDefiningClass().equals(OEM_OWNER)
                    && ref.getName().equals("mapHostIdToPresent")
                    && ref.getParameterTypes().equals(List.of(STRING))
                    && ref.getReturnType().equals(STRING)) {
                var call = (Instruction3rc) insn;
                check(target == -1 && address == 0x78 && call.getStartRegister() == 19
                        && call.getRegisterCount() == 1, "exact OEM physical port anchor");
                target = i;
            }
            address += insn.getCodeUnits();
        }
        check(target >= 0 && address == (visibility ? 0x16e : 145), "exact framework instruction layout");
        return new Anchor(owner, found, code, target);
    }

    static byte[] apply(byte[] raw, boolean visibility) throws Exception {
        var expected = classes(raw);
        expected.replaceAll((className, definition) -> ImmutableClassDef.of(definition));
        Anchor anchor = anchor(expected, visibility);
        var code = anchor.code();
        int at = anchor.index();
        if (visibility) {
            code.addInstruction(at, new BuilderInstruction35c(Opcode.INVOKE_STATIC, 2, 1, 11, 0, 0, 0,
                    new ImmutableMethodReference(SDK_HELPER, "allow", List.of("Z", VOLUME), "Z")));
            code.addInstruction(at + 1, new BuilderInstruction11x(Opcode.MOVE_RESULT, 1));
        } else {
            code.addInstruction(at, new BuilderInstruction22x(Opcode.MOVE_OBJECT_FROM16, 11, 19));
            code.addInstruction(at + 1, new BuilderInstruction35c(Opcode.INVOKE_STATIC, 4, 11, 1, 9, 3, 0,
                    new ImmutableMethodReference(OEM_HELPER, "resolve",
                            List.of(STRING, CONTEXT, "Ljava/lang/Object;", "[Ljava/lang/Object;"), STRING)));
            code.addInstruction(at + 2, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 19));
        }
        Method method = anchor.method();
        Method changed = new ImmutableMethod(method.getDefiningClass(), method.getName(),
                method.getParameters(), method.getReturnType(), method.getAccessFlags(),
                method.getAnnotations(), method.getHiddenApiRestrictions(),
                new ImmutableMethodImplementation(code.getRegisterCount(), code.getInstructions(),
                        code.getTryBlocks(), preserveDebugItems(method.getImplementation(), code)));
        ClassDef owner = anchor.owner();
        var direct = new ArrayList<Method>();
        for (var original : owner.getDirectMethods()) {
            direct.add(original.toString().equals(method.toString()) ? changed : original);
        }
        expected.put(owner.getType(), new ImmutableClassDef(owner.getType(), owner.getAccessFlags(),
                owner.getSuperclass(), owner.getInterfaces(), owner.getSourceFile(), owner.getAnnotations(),
                owner.getStaticFields(), owner.getInstanceFields(), direct, owner.getVirtualMethods()));
        byte[] result = FrameworkDexMaterializer.serialize(new ImmutableDexFile(Opcodes.forApi(30), expected.values()));
        if (visibility) {
            check(!OriginalNaviLibrariesGuard.hidden(raw) && !OriginalNaviLibrariesGuard.hidden(result), "services DEX flags");
        } else {
            result = HiddenApiOffsets.fix(raw, result);
        }
        Map<String, ClassDef> actual = classes(result);
        check(actual.keySet().equals(expected.keySet()), "exact class set");
        for (var pair : expected.entrySet()) {
            check(FrameworkDexMaterializer.semantic(pair.getValue())
                            .equals(FrameworkDexMaterializer.semantic(actual.get(pair.getKey()))),
                    "framework DEX drift in " + pair.getKey());
        }
        return result;
    }

    static List<DebugItem> preserveDebugItems(MethodImplementation origin,
            MutableMethodImplementation changed) {
        var old = origin.getDebugItems().iterator();
        var result = new ArrayList<DebugItem>();
        for (DebugItem item : changed.getDebugItems()) {
            check(old.hasNext(), "extra debug item");
            DebugItem before = old.next();
            check(item.getDebugItemType() == before.getDebugItemType(), "debug item order");
            if (item instanceof EndLocal local && before instanceof EndLocal previous) {
                result.add(new ImmutableEndLocal(item.getCodeAddress(), local.getRegister(),
                        previous.getName(), previous.getType(), previous.getSignature()));
            } else if (item instanceof RestartLocal local && before instanceof RestartLocal previous) {
                result.add(new ImmutableRestartLocal(item.getCodeAddress(), local.getRegister(),
                        previous.getName(), previous.getType(), previous.getSignature()));
            } else {
                result.add(item);
            }
        }
        check(!old.hasNext(), "missing debug item");
        return result;
    }

    private static Map<String, ClassDef> classes(byte[] dex) {
        check(dex != null, "primary DEX required");
        Map<String, ClassDef> byType = new TreeMap<>();
        for (ClassDef cls : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            check(byType.put(cls.getType(), cls) == null, "duplicate class");
        }
        return byType;
    }

    private static void check(boolean valid, String message) {
        Failure.require(valid, "FREE731_USB", message, 3);
    }
}
