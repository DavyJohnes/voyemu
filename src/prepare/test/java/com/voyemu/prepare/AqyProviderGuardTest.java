package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;

import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;

public final class AqyProviderGuardTest {

    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static void verify(Method original, Method patched) {
        var before = new Api30NetworkNullGuardTest.Body(original);
        var after = new Api30NetworkNullGuardTest.Body(patched);
        int guard = findGuard(after);

        checkGuardInstructions(before, after, guard);
        checkPreservedFlow(before, after, guard);
        check(
                before.instructions().get(before.index().get(0x2)).getOpcode()
                                == Opcode.INVOKE_DIRECT
                        && ((ReferenceInstruction)
                                        before.instructions().get(before.index().get(0x2)))
                                .getReference()
                                .toString()
                                .contains("VideoBasePresent;-><init>"),
                "real superclass initialized first");
        check(
                before.instructions().get(before.index().get(0x1a)).getOpcode()
                                == Opcode.IPUT_OBJECT
                        && ((ReferenceInstruction)
                                        before.instructions().get(before.index().get(0x1a)))
                                .getReference()
                                .toString()
                                .contains("->rankOb:"),
                "observer initialized before optional registration");
        var overwritten = before.instructions().get(before.index().get(0xd8));
        check(
                overwritten.getOpcode() == Opcode.IGET_OBJECT
                        && ((TwoRegisterInstruction) overwritten).getRegisterA() == 2,
                "original path overwrites scratch log register before observation");
        check(
                patched.getImplementation().getRegisterCount() == 7
                        && patched.getImplementation().getTryBlocks().isEmpty(),
                "no new register or broad catch");
    }

    private static int findGuard(Api30NetworkNullGuardTest.Body after) {
        int guard = -1;

        for (int i = 0; i < after.instructions().size(); i++) {
            if (after.instructions().get(i) instanceof ReferenceInstruction ref
                    && ref.getReference()
                            .toString()
                            .equals(
                                    "Landroid/content/Context;->getPackageManager()Landroid/content/pm/PackageManager;")) {
                check(guard == -1, "one real provider check");
                guard = i;
            }
        }

        check(guard >= 0, "actual provider guard missing");

        return guard;
    }

    private static void checkGuardInstructions(
            Api30NetworkNullGuardTest.Body before,
            Api30NetworkNullGuardTest.Body after,
            int guard) {
        check(
                after.instructions().size() == before.instructions().size() + 7,
                "only seven finite guard instructions added");
        var first = (FiveRegisterInstruction) after.instructions().get(guard);
        check(
                first.getRegisterCount() == 1 && first.getRegisterC() == 6,
                "original context preserved for actual package lookup");
        check(
                after.instructions().get(guard + 1).getOpcode() == Opcode.MOVE_RESULT_OBJECT
                        && ((OneRegisterInstruction) after.instructions().get(guard + 1))
                                        .getRegisterA()
                                == 3,
                "dead scratch manager register");
        check(
                after.instructions().get(guard + 2).getOpcode() == Opcode.CONST_STRING
                        && ((OneRegisterInstruction) after.instructions().get(guard + 2))
                                        .getRegisterA()
                                == 4
                        && ((ReferenceInstruction) after.instructions().get(guard + 2))
                                .getReference()
                                .toString()
                                .equals(AqyProviderGuard.AUTHORITY),
                "exact actual provider authority");
        check(
                after.instructions().get(guard + 3).getOpcode() == Opcode.CONST_4
                        && ((OneRegisterInstruction) after.instructions().get(guard + 3))
                                        .getRegisterA()
                                == 2,
                "only dead log register used for lookup flags");
        var resolve = (FiveRegisterInstruction) after.instructions().get(guard + 4);
        check(
                resolve.getRegisterCount() == 3
                        && resolve.getRegisterC() == 3
                        && resolve.getRegisterD() == 4
                        && resolve.getRegisterE() == 2
                        && ((ReferenceInstruction) resolve)
                                .getReference()
                                .toString()
                                .equals(
                                        "Landroid/content/pm/PackageManager;->resolveContentProvider(Ljava/lang/String;I)Landroid/content/pm/ProviderInfo;"),
                "real provider availability, no fake content");
        check(
                after.instructions().get(guard + 5).getOpcode() == Opcode.MOVE_RESULT_OBJECT
                        && ((OneRegisterInstruction) after.instructions().get(guard + 5))
                                        .getRegisterA()
                                == 3
                        && after.instructions().get(guard + 6).getOpcode() == Opcode.IF_EQZ
                        && ((OneRegisterInstruction) after.instructions().get(guard + 6))
                                        .getRegisterA()
                                == 3,
                "only absent provider takes original return");
    }

    private static void checkPreservedFlow(
            Api30NetworkNullGuardTest.Body before,
            Api30NetworkNullGuardTest.Body after,
            int guard) {
        Map<Integer, Integer> oldMap = new HashMap<>(), newMap = new HashMap<>();

        for (int i = 0; i < before.instructions().size(); i++) {
            oldMap.put(i, i);
        }

        for (int i = 0, retained = 0; i < after.instructions().size(); i++) {
            if (i < guard || i >= guard + 7) {
                newMap.put(i, retained++);
            }
        }

        for (var item : newMap.entrySet()) {
            check(
                    Api30NetworkNullGuardTest.signature(before, item.getValue(), oldMap)
                            .equals(
                                    Api30NetworkNullGuardTest.signature(
                                            after, item.getKey(), newMap)),
                    "all original initialization, genuine observer and branches preserved");
        }

        int branch = guard + 6,
                target =
                        after.index()
                                .get(
                                        after.addresses().get(branch)
                                                + ((OffsetInstruction)
                                                                after.instructions().get(branch))
                                                        .getCodeOffset());
        check(
                newMap.get(target).equals(before.index().get(0xdd))
                        && after.instructions().get(target).getOpcode() == Opcode.RETURN_VOID,
                "absent path is exact original return");
        check(
                newMap.get(guard + 7).equals(before.index().get(0xd4)),
                "genuine provider executes original resolver/observer path");
    }

    interface Checked {
        void run() throws Exception;
    }

    static void reject(Checked action) throws Exception {
        boolean denied = false;

        try {
            action.run();
        } catch (Failure e) {
            check(e.code.equals("AQY_PROVIDER_CONTRACT"), "precise pinned boundary failure");
            denied = true;
        }

        check(denied, "wrong contract rejected");
    }

    public static void main(String[] args) throws Exception {
        check(args.length == 1 || args.length == 2, "ROOT [--original|PREPARED_APK]");
        Path root = Path.of(args[0]).toRealPath();
        Map<String, Object>
                constants =
                        TestSteps.select(
                                root,
                                "aqy_boundary",
                                true,
                                AqyProviderGuard.NAME,
                                LauncherPowerGuard.NAME),
                other =
                        TestSteps.select(
                                root,
                                "aqy_boundary",
                                false,
                                AqyProviderGuard.NAME,
                                LauncherPowerGuard.NAME);
        Path original = TestSteps.original(root, constants, "Launcher.apk"),
                foreign = TestSteps.original(root, other, "Launcher.apk");
        byte[] source;

        try (var zip = new java.util.zip.ZipFile(original.toFile())) {
            source = zip.getInputStream(zip.getEntry("classes2.dex")).readAllBytes();
        }

        if (args.length == 2 && args[1].equals("--original")) {
            verify(AqyProviderGuard.method(source), AqyProviderGuard.method(source));

            return;
        }

        byte[] prior = LauncherPowerGuard.apply(constants, 30, original, source, "classes2.dex");
        check(
                java.util.Arrays.equals(
                        source, AqyProviderGuard.validateOriginal(constants, 30, original)),
                "second recipe validation reads pinned original despite current predecessor");
        byte[] patched = AqyProviderGuard.apply(constants, 30, original, prior);
        verify(AqyProviderGuard.method(source), AqyProviderGuard.method(patched));
        reject(() -> AqyProviderGuard.validate(other, 30, original, source));
        reject(() -> AqyProviderGuard.validate(constants, 28, original, source));
        reject(() -> AqyProviderGuard.validate(constants, 30, original, null));
        reject(() -> AqyProviderGuard.validate(constants, 30, original, patched));
        reject(() ->
                AqyProviderGuard.validate(
                        constants, 30, original.resolveSibling("SystemService.apk"), source));
        reject(() -> AqyProviderGuard.validate(constants, 30, foreign, source));
        reject(() -> AqyProviderGuard.apply(constants, 30, original, null));
        reject(() -> AqyProviderGuard.apply(constants, 30, original, patched));

        if (args.length == 2) {
            verify(
                    AqyProviderGuard.method(source),
                    AqyProviderGuard.method(readPrepared(Path.of(args[1]))));
        }

        System.out.println(
                Json.write(
                        Map.of(
                                "original_paths_retained",
                                true,
                                "actual_provider_presence_checked",
                                true,
                                "only_optional_observer_skipped_when_absent",
                                true,
                                "nine_negative_contracts_rejected",
                                true,
                                "guest_started",
                                false)));
    }

    private static byte[] readPrepared(Path apk) throws Exception {
        try (var zip = new java.util.zip.ZipFile(apk.toFile())) {
            return zip.getInputStream(zip.getEntry("classes2.dex")).readAllBytes();
        }
    }
}
