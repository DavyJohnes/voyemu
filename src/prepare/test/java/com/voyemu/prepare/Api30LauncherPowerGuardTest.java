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
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Actual owner-Dex map boundary; this does not implement or accept automotive power support. */
public final class Api30LauncherPowerGuardTest {

    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static Method method(DexFile dex, String entry) {
        Method found = null;

        for (var type : dex.getClasses()) {
            if (type.getType().equals(LauncherPowerGuard.owner(entry))) {
                for (var candidate : type.getMethods()) {
                    if (candidate
                                    .getName()
                                    .equals(LauncherPowerGuard.wrapper(entry) ? "<init>" : "init")
                            && candidate.getParameterTypes().isEmpty()) {
                        check(found == null, "one exact map init");
                        found = candidate;
                    }
                }
            }
        }

        check(found != null, "actual map owner method");

        return found;
    }

    static void verify(Method original, Method patched, String entry) {
        boolean wrapper = LauncherPowerGuard.wrapper(entry);
        int start = wrapper ? 0xa : 0x1d, finish = wrapper ? 0x19 : 0x3d;
        var before = new Api30NetworkNullGuardTest.Body(original);
        var after = new Api30NetworkNullGuardTest.Body(patched);
        int guard = findGuard(after);
        var added = List.of(guard, guard + 1, guard + 2, guard + 3);

        checkGuardInstructions(before, after, guard);
        checkScratchRegister(before, wrapper, start);
        Map<Integer, Integer> oldMap = new HashMap<>(), newMap = new HashMap<>();

        for (int i = 0; i < before.instructions().size(); i++) {
            oldMap.put(i, i);
        }

        for (int i = 0, retained = 0; i < after.instructions().size(); i++) {
            if (!added.contains(i)) {
                newMap.put(i, retained++);
            }
        }

        for (var retained : newMap.entrySet()) {
            check(
                    Api30NetworkNullGuardTest.signature(before, retained.getValue(), oldMap)
                            .equals(
                                    Api30NetworkNullGuardTest.signature(
                                            after, retained.getKey(), newMap)),
                    "all original receiver/LBS and genuine power instructions/targets retained");
        }

        int branch = guard + 3;
        int target =
                after.index()
                        .get(
                                after.addresses().get(branch)
                                        + ((OffsetInstruction) after.instructions().get(branch))
                                                .getCodeOffset());
        check(
                newMap.get(target).equals(before.index().get(finish))
                        && after.instructions().get(target).getOpcode() == Opcode.RETURN_VOID,
                "absent CarService returns at original return");
        check(
                newMap.get(guard + 4).equals(before.index().get(start)),
                "present CarService preserves exact genuine power path");
        check(
                patched.getImplementation().getRegisterCount() == 6
                        && patched.getImplementation().getTryBlocks().isEmpty(),
                "no extra registers or broad exception handling");
    }

    private static int findGuard(Api30NetworkNullGuardTest.Body after) {
        int guard = -1;

        for (int i = 0; i < after.instructions().size(); i++) {
            var item = after.instructions().get(i);

            if (item.getOpcode() == Opcode.CONST_STRING
                    && ((ReferenceInstruction) item)
                            .getReference()
                            .toString()
                            .equals("car_service")) {
                check(guard == -1, "one exact service check");
                guard = i;
            }
        }

        check(guard >= 0, "missing automotive service must bypass only original map power block");

        return guard;
    }

    private static void checkGuardInstructions(
            Api30NetworkNullGuardTest.Body before,
            Api30NetworkNullGuardTest.Body after,
            int guard) {
        check(
                after.instructions().size() == before.instructions().size() + 4,
                "only four guard instructions added");
        check(
                ((OneRegisterInstruction) after.instructions().get(guard)).getRegisterA() == 0,
                "dead scratch register");
        check(
                after.instructions().get(guard + 1).getOpcode() == Opcode.INVOKE_STATIC
                        && ((ReferenceInstruction) after.instructions().get(guard + 1))
                                .getReference()
                                .toString()
                                .equals(
                                        "Landroid/os/ServiceManager;->checkService(Ljava/lang/String;)Landroid/os/IBinder;"),
                "real nonblocking service presence check");
        check(
                after.instructions().get(guard + 2).getOpcode() == Opcode.MOVE_RESULT_OBJECT
                        && ((OneRegisterInstruction) after.instructions().get(guard + 2))
                                        .getRegisterA()
                                == 0,
                "actual binder result");
        check(
                after.instructions().get(guard + 3).getOpcode() == Opcode.IF_EQZ
                        && ((OneRegisterInstruction) after.instructions().get(guard + 3))
                                        .getRegisterA()
                                == 0,
                "only absent binder branches");
    }

    private static void checkScratchRegister(
            Api30NetworkNullGuardTest.Body before, boolean wrapper, int start) {
        if (wrapper) {
            check(
                    before.instructions().get(0).getOpcode() == Opcode.INVOKE_DIRECT
                            && ((ReferenceInstruction) before.instructions().get(0))
                                    .getReference()
                                    .toString()
                                    .equals("Ljava/lang/Object;-><init>()V"),
                    "real superclass constructor remains first");
            check(
                    before.instructions().get(before.index().get(8)).getOpcode()
                                    == Opcode.IPUT_OBJECT
                            && ((ReferenceInstruction)
                                            before.instructions().get(before.index().get(8)))
                                    .getReference()
                                    .toString()
                                    .contains("->mListeners:Ljava/util/List;"),
                    "listener field initialized before optional power boundary");
            check(
                    before.instructions().get(before.index().get(start)).getOpcode()
                                    == Opcode.INVOKE_STATIC
                            && ((org.jf.dexlib2.iface.instruction.FiveRegisterInstruction)
                                                    before.instructions()
                                                            .get(before.index().get(start)))
                                            .getRegisterCount()
                                    == 0
                            && before.instructions().get(before.index().get(start) + 1).getOpcode()
                                    == Opcode.MOVE_RESULT_OBJECT
                            && ((OneRegisterInstruction)
                                                    before.instructions()
                                                            .get(before.index().get(start) + 1))
                                            .getRegisterA()
                                    == 0,
                    "scratch v0 not read before original getContext result overwrites it");
        } else {
            check(
                    before.instructions().get(before.index().get(start)).getOpcode()
                                    == Opcode.IGET_OBJECT
                            && ((TwoRegisterInstruction)
                                                    before.instructions()
                                                            .get(before.index().get(start)))
                                            .getRegisterA()
                                    == 0
                            && ((TwoRegisterInstruction)
                                                    before.instructions()
                                                            .get(before.index().get(start)))
                                            .getRegisterB()
                                    == 5,
                    "original map continuation overwrites scratch v0 before reading it");
        }
    }

    interface Checked {
        void run() throws Exception;
    }

    static void reject(Checked action) throws Exception {
        boolean denied = false;

        try {
            action.run();
        } catch (Failure expected) {
            check(expected.code.equals("LAUNCHER_POWER_CONTRACT"), "precise map failure");
            denied = true;
        }

        check(denied, "unsupported map mutation rejected");
    }

    public static void main(String[] args) throws Exception {
        check(
                args.length == 1 || args.length == 2,
                "ROOT [--original-map|--original-wrapper|PREPARED_LAUNCHER_APK]");
        Path root = Path.of(args[0]).toRealPath();
        Map<String, Object>
                constants =
                        TestSteps.select(
                                root, "launcher_power_boundary", false, LauncherPowerGuard.NAME),
                other =
                        TestSteps.select(
                                root, "launcher_power_boundary", true, LauncherPowerGuard.NAME);
        Path original = TestSteps.original(root, constants, "Launcher.apk");
        check(
                FilesSupport.hash(original).equals(StepConstants.pins(constants).get(0)),
                "exact Launcher owner pin");
        boolean red = args.length == 2 && args[1].startsWith("--original-");

        for (String entry : List.of("classes.dex", "classes2.dex")) {
            if (red
                    && !entry.equals(
                            args[1].endsWith("wrapper") ? "classes2.dex" : "classes.dex")) {
                continue;
            }

            byte[] before;

            try (var zip = new java.util.zip.ZipFile(original.toFile())) {
                before = zip.getInputStream(zip.getEntry(entry)).readAllBytes();
            }

            if (red) {
                verify(
                        method(new DexBackedDexFile(Opcodes.forApi(30), before), entry),
                        method(new DexBackedDexFile(Opcodes.forApi(30), before), entry),
                        entry);

                return;
            }

            byte[] after = LauncherPowerGuard.apply(constants, 30, original, before, entry);
            verify(
                    method(new DexBackedDexFile(Opcodes.forApi(30), before), entry),
                    method(new DexBackedDexFile(Opcodes.forApi(30), after), entry),
                    entry);
            reject(() -> LauncherPowerGuard.validate(other, 30, original, before, entry));
            reject(() -> LauncherPowerGuard.validate(constants, 28, original, before, entry));
            reject(() -> LauncherPowerGuard.validate(constants, 30, original, null, entry));
            reject(() ->
                    LauncherPowerGuard.validate(
                            constants,
                            30,
                            original.resolveSibling("SystemService.apk"),
                            before,
                            entry));

            if (args.length == 2) {
                verify(
                        method(new DexBackedDexFile(Opcodes.forApi(30), before), entry),
                        method(DexOperations.dex(Path.of(args[1]), entry, 30), entry),
                        entry);
            }
        }

        check(
                FilesSupport.hash(original).equals(StepConstants.pins(constants).get(0)),
                "original unchanged");
        System.out.println(
                Json.write(
                        Map.of(
                                "original_instruction_paths_retained",
                                true,
                                "startup_gateways",
                                2,
                                "car_power_lifecycle_supported",
                                false,
                                "twelve_unsupported_mutations_rejected",
                                true,
                                "constructor_and_listener_initialization_preserved",
                                true,
                                "guest_started",
                                false)));
    }
}
