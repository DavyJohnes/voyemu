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
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
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
    static final String QGAPI_PIN =
            "c178682be164be4872697dbb5accdc6ea28446941b6d681ddb736f2fdb627c07";
    static final String VIDEO_PIN =
            "11a3bd57f07d7d4757a904ee49eb7124663ea73d841e5c51e39f1207fc56feee";
    static final String GALLERY_PIN =
            "c30633d39bb4127ac7fcd79300f00f2be608ff6b59cb24ebf855471e967be537";

    enum Layout {
        SERVICES(12, 0x16e, 0x120, 1),
        QGAPI(23, 145, 0x78, 19),
        VIDEO(23, 148, 0x7b, 19),
        GALLERY(14, 128, 0x71, 5);
        final int registers, codeUnits, anchorAddress, hostIdRegister;

        Layout(int registers, int codeUnits, int anchorAddress, int hostIdRegister) {
            this.registers = registers;
            this.codeUnits = codeUnits;
            this.anchorAddress = anchorAddress;
            this.hostIdRegister = hostIdRegister;
        }
    }

    private static final String SDK_OWNER = "Lcom/android/server/StorageManagerService;";
    private static final String OEM_OWNER = "Lcom/qinggan/os/ExternalDevicesState;";
    private static final String VOLUME = "Landroid/os/storage/VolumeInfo;";
    private static final String DISK = "Landroid/os/storage/DiskInfo;";
    private static final String OEM_HELPER = "Lcom/voyemu/qgapi/VoyemuUsbPort;";
    private static final String SDK_HELPER = "Lcom/voyemu/storage/UsbMountVisibility;";
    private static final String STRING = "Ljava/lang/String;";
    private static final String CONTEXT = "Landroid/content/Context;";

    private Free731UsbGuard() {}

    private record Anchor(
            ClassDef owner, Method method, MutableMethodImplementation code, int index) {}

    static void checkInput(byte[] raw, Layout layout) {
        anchor(classes(raw), layout);
    }

    private static Anchor anchor(Map<String, ClassDef> dex, Layout layout) {
        boolean visibility = layout == Layout.SERVICES;
        var owner = dex.get(visibility ? SDK_OWNER : OEM_OWNER);

        check(
                owner != null && !dex.containsKey(visibility ? SDK_HELPER : OEM_HELPER),
                "framework owner");
        Method found = null;

        for (var method : owner.getDirectMethods()) {
            boolean match =
                    visibility
                            ? method.getName().equals("onVolumeCreatedLocked")
                                    && method.getParameterTypes().equals(List.of(VOLUME))
                                    && method.getReturnType().equals("V")
                            : method.getName().equals("getVolumeList")
                                    && method.getParameterTypes().equals(List.of(CONTEXT))
                                    && method.getReturnType().equals("Ljava/util/List;");

            if (match) {
                check(
                        found == null
                                && method.getImplementation() != null
                                && method.getAccessFlags() == (visibility ? 2 : 9)
                                && method.getImplementation().getRegisterCount()
                                        == layout.registers,
                        "unique framework method");
                found = method;
            }
        }

        check(found != null, "framework method missing");

        var code = new MutableMethodImplementation(found.getImplementation());
        int target = visibility ? findVisibilityAnchor(code, layout) : findPortAnchor(code, layout);

        return new Anchor(owner, found, code, target);
    }

    private static int findVisibilityAnchor(MutableMethodImplementation code, Layout layout) {
        int target = -1;
        int address = 0;

        for (int i = 0; i < code.getInstructions().size(); i++) {
            var insn = code.getInstructions().get(i);

            if (insn.getOpcode() == Opcode.INVOKE_VIRTUAL
                    && ((ReferenceInstruction) insn)
                            .getReference()
                            .toString()
                            .equals(DISK + "->isAdoptable()Z")) {
                var invoke = (Instruction35c) insn;
                check(
                        target == -1
                                && address == layout.anchorAddress
                                && invoke.getRegisterCount() == 1
                                && invoke.getRegisterC() == layout.hostIdRegister
                                && i + 2 < code.getInstructions().size(),
                        "exact vold visibility anchor");
                checkVisibilityBranch(code, i);
                target = i + 2;
            }

            address += insn.getCodeUnits();
        }

        check(target >= 0 && address == layout.codeUnits, "exact framework instruction layout");

        return target;
    }

    private static void checkVisibilityBranch(MutableMethodImplementation code, int invoke) {
        var result = code.getInstructions().get(invoke + 1);
        var branch = code.getInstructions().get(invoke + 2);

        check(
                result.getOpcode() == Opcode.MOVE_RESULT
                        && ((OneRegisterInstruction) result).getRegisterA() == 1
                        && branch.getOpcode() == Opcode.IF_EQZ
                        && ((OneRegisterInstruction) branch).getRegisterA() == 1
                        && ((OffsetInstruction) branch).getCodeOffset() == 7,
                "exact vold visibility branch");
    }

    private static int findPortAnchor(MutableMethodImplementation code, Layout layout) {
        int target = -1;
        int address = 0;
        Opcode opcode =
                layout == Layout.GALLERY ? Opcode.INVOKE_STATIC : Opcode.INVOKE_STATIC_RANGE;

        for (int i = 0; i < code.getInstructions().size(); i++) {
            var insn = code.getInstructions().get(i);

            if (insn.getOpcode() == opcode && isPortMap((ReferenceInstruction) insn)) {
                boolean registers =
                        insn instanceof Instruction3rc range
                                ? range.getStartRegister() == layout.hostIdRegister
                                        && range.getRegisterCount() == 1
                                : insn instanceof Instruction35c call
                                        && call.getRegisterC() == layout.hostIdRegister
                                        && call.getRegisterCount() == 1;
                check(
                        target == -1 && address == layout.anchorAddress && registers,
                        "exact OEM physical port anchor");
                target = layout == Layout.GALLERY ? galleryHostIdInsertion(code) : i;
            }

            address += insn.getCodeUnits();
        }

        check(target >= 0 && address == layout.codeUnits, "exact framework instruction layout");

        return target;
    }

    private static boolean isPortMap(ReferenceInstruction insn) {
        return insn.getReference() instanceof MethodReference ref
                && ref.getDefiningClass().equals(OEM_OWNER)
                && ref.getName().equals("mapHostIdToPresent")
                && ref.getParameterTypes().equals(List.of(STRING))
                && ref.getReturnType().equals(STRING);
    }

    private static int galleryHostIdInsertion(MutableMethodImplementation code) {
        int address = 0;

        for (int i = 0; i < code.getInstructions().size(); i++) {
            var insn = code.getInstructions().get(i);
            address += insn.getCodeUnits();

            if (address == 0x5b) {
                check(
                        insn.getOpcode() == Opcode.CHECK_CAST
                                && ((OneRegisterInstruction) insn).getRegisterA() == 5
                                && ((ReferenceInstruction) insn)
                                        .getReference()
                                        .toString()
                                        .equals(STRING),
                        "Gallery host ID cast");

                return i + 1;
            }
        }

        throw new Failure("FREE731_USB", "Gallery host ID cast missing", 3);
    }

    static byte[] apply(byte[] raw, Layout layout) throws Exception {
        boolean visibility = layout == Layout.SERVICES;
        var expected = classes(raw);

        expected.replaceAll((className, definition) -> ImmutableClassDef.of(definition));
        Anchor anchor = anchor(expected, layout);
        var code = anchor.code();
        int at = anchor.index();

        if (visibility) {
            code.addInstruction(
                    at,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_STATIC,
                            2,
                            1,
                            11,
                            0,
                            0,
                            0,
                            new ImmutableMethodReference(
                                    SDK_HELPER, "allow", List.of("Z", VOLUME), "Z")));
            code.addInstruction(at + 1, new BuilderInstruction11x(Opcode.MOVE_RESULT, 1));
        } else {
            boolean gallery = layout == Layout.GALLERY;
            int first = gallery ? 5 : 11;

            if (!gallery) {
                code.addInstruction(
                        at++, new BuilderInstruction22x(Opcode.MOVE_OBJECT_FROM16, first, 19));
            }

            code.addInstruction(
                    at,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_STATIC,
                            3,
                            first,
                            gallery ? 4 : 9,
                            gallery ? 13 : 3,
                            0,
                            0,
                            new ImmutableMethodReference(
                                    OEM_HELPER,
                                    "resolve",
                                    List.of(STRING, "Ljava/lang/Object;", "[Ljava/lang/Object;"),
                                    STRING)));
            code.addInstruction(
                    at + 1,
                    new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, layout.hostIdRegister));
        }

        Method method = anchor.method();
        Method changed =
                new ImmutableMethod(
                        method.getDefiningClass(),
                        method.getName(),
                        method.getParameters(),
                        method.getReturnType(),
                        method.getAccessFlags(),
                        method.getAnnotations(),
                        method.getHiddenApiRestrictions(),
                        new ImmutableMethodImplementation(
                                code.getRegisterCount(),
                                code.getInstructions(),
                                code.getTryBlocks(),
                                preserveDebugItems(method.getImplementation(), code)));
        ClassDef owner = anchor.owner();
        var direct = new ArrayList<Method>();

        for (var original : owner.getDirectMethods()) {
            direct.add(original.toString().equals(method.toString()) ? changed : original);
        }

        expected.put(
                owner.getType(),
                new ImmutableClassDef(
                        owner.getType(),
                        owner.getAccessFlags(),
                        owner.getSuperclass(),
                        owner.getInterfaces(),
                        owner.getSourceFile(),
                        owner.getAnnotations(),
                        owner.getStaticFields(),
                        owner.getInstanceFields(),
                        direct,
                        owner.getVirtualMethods()));

        if (layout == Layout.VIDEO || layout == Layout.GALLERY) {
            FrameworkDexMaterializer.normalizeTryBlocks(expected);
        }

        byte[] result =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), expected.values()));

        if (OriginalNaviLibrariesGuard.hidden(raw)) {
            check(layout == Layout.QGAPI, "unexpected hidden DEX flags");
            result = HiddenApiOffsets.fix(raw, result);
        } else {
            check(
                    !OriginalNaviLibrariesGuard.hidden(result),
                    "unexpected encoded hidden DEX flags");
        }

        Map<String, ClassDef> actual = classes(result);
        check(actual.keySet().equals(expected.keySet()), "exact class set");

        for (var pair : expected.entrySet()) {
            check(
                    FrameworkDexMaterializer.semantic(pair.getValue())
                            .equals(FrameworkDexMaterializer.semantic(actual.get(pair.getKey()))),
                    "framework DEX drift in " + pair.getKey());
        }

        return result;
    }

    static List<DebugItem> preserveDebugItems(
            MethodImplementation origin, MutableMethodImplementation changed) {
        var old = origin.getDebugItems().iterator();
        var result = new ArrayList<DebugItem>();

        for (DebugItem item : changed.getDebugItems()) {
            check(old.hasNext(), "extra debug item");
            DebugItem before = old.next();
            check(item.getDebugItemType() == before.getDebugItemType(), "debug item order");

            if (item instanceof EndLocal local && before instanceof EndLocal previous) {
                result.add(
                        new ImmutableEndLocal(
                                item.getCodeAddress(),
                                local.getRegister(),
                                previous.getName(),
                                previous.getType(),
                                previous.getSignature()));
            } else if (item instanceof RestartLocal local
                    && before instanceof RestartLocal previous) {
                result.add(
                        new ImmutableRestartLocal(
                                item.getCodeAddress(),
                                local.getRegister(),
                                previous.getName(),
                                previous.getType(),
                                previous.getSignature()));
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
