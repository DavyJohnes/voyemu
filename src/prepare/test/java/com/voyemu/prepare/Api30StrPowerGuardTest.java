package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Actual owner-Dex STR boundary; this does not implement or accept automotive power support. */
public final class Api30StrPowerGuardTest {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1";

    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static Method method(DexFile dex) {
        Method found = null;

        for (var type : dex.getClasses()) {
            if (type.getType().equals(StrPowerGuard.OWNER)) {
                for (var candidate : type.getVirtualMethods()) {
                    if (candidate.getName().equals("init")) {
                        check(found == null, "one exact STR init");
                        found = candidate;
                    }
                }
            }
        }

        check(found != null, "actual STR owner method");

        return found;
    }

    static void verify(Method original, Method patched) {
        var before = new Api30NetworkNullGuardTest.Body(original);
        var after = new Api30NetworkNullGuardTest.Body(patched);
        var added = new ArrayList<Integer>();
        int nullCar = -1, typeCheck = -1, typeBranch = -1;

        for (int i = 0; i < after.instructions().size(); i++) {
            var instruction = after.instructions().get(i);

            if (instruction.getOpcode() == Opcode.IF_EQZ
                    && ((OneRegisterInstruction) instruction).getRegisterA() == 5) {
                check(nullCar == -1, "one null Car branch");
                nullCar = i;
                added.add(i);
            }

            if (instruction.getOpcode() == Opcode.INSTANCE_OF) {
                check(typeCheck == -1, "one actual capability test");
                typeCheck = i;
                typeBranch = i + 1;
                added.add(i);
                added.add(i + 1);
                check(
                        ((TwoRegisterInstruction) instruction).getRegisterA() == 0
                                && ((TwoRegisterInstruction) instruction).getRegisterB() == 5
                                && ((ReferenceInstruction) instruction)
                                        .getReference()
                                        .toString()
                                        .equals(StrPowerGuard.MANAGER),
                        "manager test uses exact live registers/type");
            }
        }

        check(
                nullCar >= 0 && typeCheck >= 0 && typeBranch < after.instructions().size(),
                "STR boundary must reject null Car and unsupported manager before original cast");
        check(
                after.instructions().get(typeBranch).getOpcode() == Opcode.IF_EQZ
                        && ((OneRegisterInstruction) after.instructions().get(typeBranch))
                                        .getRegisterA()
                                == 0,
                "manager null/plain Object return on failed instance-of");
        check(
                after.instructions().size() == before.instructions().size() + 3,
                "only three guards added");
        Map<Integer, Integer> oldMap = new HashMap<>(), newMap = new HashMap<>();

        for (int i = 0; i < before.instructions().size(); i++) {
            oldMap.put(i, i);
        }

        for (int i = 0, retained = 0; i < after.instructions().size(); i++) {
            if (!added.contains(i)) {
                newMap.put(i, retained++);
            }
        }

        for (var entry : newMap.entrySet()) {
            check(
                    Api30NetworkNullGuardTest.signature(before, entry.getValue(), oldMap)
                            .equals(
                                    Api30NetworkNullGuardTest.signature(
                                            after, entry.getKey(), newMap)),
                    "all genuine typed-path instructions and branch targets retained");
        }

        for (int branch : List.of(nullCar, typeBranch)) {
            int target =
                    after.index()
                            .get(
                                    after.addresses().get(branch)
                                            + ((OffsetInstruction) after.instructions().get(branch))
                                                    .getCodeOffset());
            check(
                    newMap.get(target).equals(before.index().get(0x1c))
                            && after.instructions().get(target).getOpcode() == Opcode.RETURN_VOID,
                    "unsupported paths return without installing a power listener");
        }

        check(
                newMap.get(nullCar + 1).equals(before.index().get(0xc)),
                "non-null Car retains original query");
        check(
                newMap.get(typeBranch + 1).equals(before.index().get(0x12)),
                "genuine manager retains exact cast/listener/setListener path");
        // DEX instance-of is false for both null and java.lang.Object; no typed replacement is
        // injected.
        check(
                patched.getImplementation().getRegisterCount() == 6
                        && patched.getImplementation().getTryBlocks().isEmpty(),
                "no extra registers or broad exception handling");
    }

    interface Checked {
        void run() throws Exception;
    }

    static void reject(Checked action) throws Exception {
        boolean denied = false;

        try {
            action.run();
        } catch (Failure expected) {
            check(expected.code.equals("STR_POWER_CONTRACT"), "precise STR failure");
            denied = true;
        }

        check(denied, "unsupported STR mutation rejected");
    }

    public static void main(String[] args) throws Exception {
        check(args.length == 1 || args.length == 2, "ROOT [--original|PREPARED_SETTING_APK]");
        Path root = Path.of(args[0]).toRealPath();
        Path original = root.resolve(".local/profiles/" + PROFILE + "/originals/Setting.apk");
        check(
                FilesSupport.hash(original).equals(StrPowerGuard.pins(PROFILE).get(0)),
                "exact Setting owner pin");
        byte[] before;

        try (var zip = new java.util.zip.ZipFile(original.toFile())) {
            before = zip.getInputStream(zip.getEntry("classes2.dex")).readAllBytes();
        }

        if (args.length == 2 && args[1].equals("--original")) {
            verify(
                    method(new DexBackedDexFile(Opcodes.forApi(30), before)),
                    method(new DexBackedDexFile(Opcodes.forApi(30), before)));

            return;
        }

        byte[] after = StrPowerGuard.apply(PROFILE, 30, original, before);
        verify(
                method(new DexBackedDexFile(Opcodes.forApi(30), before)),
                method(new DexBackedDexFile(Opcodes.forApi(30), after)));
        reject(() -> StrPowerGuard.validate("other", 30, original, before));
        reject(() -> StrPowerGuard.validate(PROFILE, 28, original, before));
        reject(() -> StrPowerGuard.validate(PROFILE, 30, original, null));
        reject(() ->
                StrPowerGuard.validate(
                        PROFILE,
                        30,
                        root.resolve(".local/profiles/" + PROFILE + "/originals/SystemService.apk"),
                        before));

        if (args.length == 2) {
            verify(
                    method(new DexBackedDexFile(Opcodes.forApi(30), before)),
                    method(DexOperations.dex(Path.of(args[1]), "classes2.dex", 30)));
        }

        check(
                FilesSupport.hash(original).equals(StrPowerGuard.pins(PROFILE).get(0)),
                "original unchanged");
        System.out.println(
                Json.write(
                        Map.of(
                                "original_instruction_paths_retained",
                                true,
                                "unsupported_paths",
                                List.of("null Car", "null manager", "plain Object manager"),
                                "str_power_lifecycle_supported",
                                false,
                                "six_unsupported_mutations_rejected",
                                true,
                                "guest_started",
                                false)));
    }
}
