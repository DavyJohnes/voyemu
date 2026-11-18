package com.voyemu.prepare;

import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.ThreeRegisterInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;
import org.jf.dexlib2.iface.instruction.formats.Instruction35c;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Real owner-Dex regressions preserve both nullable queries and connectivity broadcasts. */
public final class Api30NetworkNullGuardTest {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1";
    static final String PIN = "3b38818c46903a3786df70793712c2d2d043cfb516279b49fa412acf4a1e9c9c";
    static final String OWNER = "Lcom/qinggan/systemservice/displaymanager/DisplayManagerImpl;";
    static final String QUERY =
            "Landroid/net/ConnectivityManager;->getNetworkInfo(I)Landroid/net/NetworkInfo;";

    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static Method method(Path apk) throws Exception {
        return method(DexOperations.dex(apk, "classes.dex", 30));
    }

    static Method method(DexFile dex) throws Exception {
        Method selected = null;

        for (var type : dex.getClasses()) {
            if (type.getType().equals(OWNER)) {
                for (var method : type.getDirectMethods()) {
                    if (method.getName().equals("isNetWorkConnected")) {
                        check(
                                selected == null
                                        && method.getParameterTypes().isEmpty()
                                        && method.getReturnType().equals("Z"),
                                "exact private network query method");
                        selected = method;
                    }
                }
            }
        }

        check(
                selected != null && selected.getImplementation() != null,
                "actual owner network method required");

        return selected;
    }

    static Method receiver(DexFile dex) {
        Method selected = null;

        for (var type : dex.getClasses()) {
            if (type.getType()
                    .equals("Lcom/qinggan/systemservice/displaymanager/DisplayManagerImpl$13;")) {
                for (var candidate : type.getVirtualMethods()) {
                    if (candidate.getName().equals("onReceive")) {
                        check(
                                selected == null
                                        && candidate
                                                .getParameterTypes()
                                                .equals(
                                                        List.of(
                                                                "Landroid/content/Context;",
                                                                "Landroid/content/Intent;"))
                                        && candidate.getReturnType().equals("V"),
                                "exact connectivity receiver");
                        selected = candidate;
                    }
                }
            }
        }

        check(
                selected != null && selected.getImplementation() != null,
                "actual owner connectivity receiver required");

        return selected;
    }

    record Body(
            List<Instruction> instructions, List<Integer> addresses, Map<Integer, Integer> index) {
        Body(Method method) {
            this(new ArrayList<>(), new ArrayList<>(), new HashMap<>());
            int address = 0;

            for (var instruction : method.getImplementation().getInstructions()) {
                index.put(address, instructions.size());
                addresses.add(address);
                instructions.add(instruction);
                address += instruction.getCodeUnits();
            }
        }
    }

    static String signature(Body body, int index, Map<Integer, Integer> retained) {
        var instruction = body.instructions.get(index);
        String result = instruction.getOpcode().name();

        if (instruction instanceof Instruction35c i) {
            result +=
                    " args="
                            + i.getRegisterCount()
                            + ","
                            + i.getRegisterC()
                            + ","
                            + i.getRegisterD()
                            + ","
                            + i.getRegisterE()
                            + ","
                            + i.getRegisterF()
                            + ","
                            + i.getRegisterG();
        } else if (instruction instanceof ThreeRegisterInstruction i) {
            result += " regs=" + i.getRegisterA() + "," + i.getRegisterB() + "," + i.getRegisterC();
        } else if (instruction instanceof TwoRegisterInstruction i) {
            result += " regs=" + i.getRegisterA() + "," + i.getRegisterB();
        } else if (instruction instanceof OneRegisterInstruction i) {
            result += " reg=" + i.getRegisterA();
        }

        if (instruction instanceof NarrowLiteralInstruction i) {
            result += " literal=" + i.getNarrowLiteral();
        }

        if (instruction instanceof ReferenceInstruction i) {
            result += " reference=" + i.getReference();
        }

        if (instruction instanceof OffsetInstruction i) {
            Integer target = body.index.get(body.addresses.get(index) + i.getCodeOffset());
            check(
                    target != null && retained.containsKey(target),
                    "branch must target retained OEM instruction");
            result += " target=" + retained.get(target);
        }

        return result;
    }

    static void verify(Method original, Method patched) {
        check(
                patched.getAccessFlags() == original.getAccessFlags(),
                "original private method flags preserved");
        check(
                original.getImplementation().getRegisterCount() == 5
                        && patched.getImplementation().getRegisterCount() == 5,
                "network register geometry preserved");
        check(
                original.getImplementation().getTryBlocks().isEmpty()
                        && patched.getImplementation().getTryBlocks().isEmpty(),
                "no broad exception wrapper");
        var before = new Body(original);
        var after = new Body(patched);
        List<Integer> added = new ArrayList<>();
        int queries = 0;

        for (int i = 0; i < after.instructions.size(); i++) {
            if (after.instructions.get(i) instanceof ReferenceInstruction r
                    && r.getReference().toString().equals(QUERY)) {
                check(
                        i + 2 < after.instructions.size()
                                && after.instructions
                                        .get(i + 1)
                                        .getOpcode()
                                        .name()
                                        .equals("MOVE_RESULT_OBJECT"),
                        "actual nullable query result");
                var guard = after.instructions.get(i + 2);
                check(
                        guard.getOpcode().name().equals("IF_EQZ")
                                && guard instanceof OneRegisterInstruction oneRegisterInstruction
                                && oneRegisterInstruction.getRegisterA() == 2
                                && guard instanceof OffsetInstruction,
                        "nullable Ethernet/WiFi query must have its own immediate zero guard");
                added.add(i + 2);
                queries++;
            }
        }

        check(
                queries == 2 && after.instructions.size() == before.instructions.size() + 2,
                "exact two added null guards only");
        Map<Integer, Integer> oldMap = new HashMap<>(), newMap = new HashMap<>();

        for (int i = 0; i < before.instructions.size(); i++) {
            oldMap.put(i, i);
        }

        for (int i = 0, retained = 0; i < after.instructions.size(); i++) {
            if (!added.contains(i)) {
                newMap.put(i, retained++);
            }
        }

        for (var entry : newMap.entrySet()) {
            check(
                    signature(before, entry.getValue(), oldMap)
                            .equals(signature(after, entry.getKey(), newMap)),
                    "existing Ethernet OR WiFi/default-false control flow must be identical");
        }

        int[] destinations = {0x16, 0x25};

        for (int i = 0; i < 2; i++) {
            int at = added.get(i);
            var branch = (OffsetInstruction) after.instructions.get(at);
            Integer target = after.index.get(after.addresses.get(at) + branch.getCodeOffset());
            check(
                    target != null
                            && newMap.containsKey(target)
                            && newMap.get(target).equals(before.index.get(destinations[i])),
                    "null Ethernet continues WiFi; null WiFi returns existing disconnected result");
        }
    }

    static void verifyReceiver(Method original, Method patched) {
        Body before = new Body(original), after = new Body(patched);
        List<Integer> added = new ArrayList<>();
        int queries = 0;
        int[] variables = {1, 0}, afterAddresses = {0xd7, 0xe7};

        for (int index = 0; index < after.instructions.size(); index++) {
            if (after.instructions.get(index) instanceof ReferenceInstruction reference
                    && reference.getReference().toString().equals(QUERY)) {
                check(queries < 2, "exact two connectivity receiver transports");
                int at = index + 2;
                check(
                        after.instructions.get(at).getOpcode() == org.jf.dexlib2.Opcode.IF_NEZ
                                && ((OneRegisterInstruction) after.instructions.get(at))
                                                .getRegisterA()
                                        == 4,
                        "connectivity receiver must guard nullable NetworkInfo before getState");
                check(
                        after.instructions.get(at + 1).getOpcode()
                                        == org.jf.dexlib2.Opcode.SGET_OBJECT
                                && ((OneRegisterInstruction) after.instructions.get(at + 1))
                                                .getRegisterA()
                                        == variables[queries]
                                && ((ReferenceInstruction) after.instructions.get(at + 1))
                                        .getReference()
                                        .toString()
                                        .equals(
                                                "Landroid/net/NetworkInfo$State;->DISCONNECTED:Landroid/net/NetworkInfo$State;"),
                        "absent transport uses actual disconnected state");
                check(
                        after.instructions.get(at + 2).getOpcode() == org.jf.dexlib2.Opcode.GOTO,
                        "null receiver result skips only original getState/move");
                added.add(at);
                added.add(at + 1);
                added.add(at + 2);
                queries++;
            }
        }

        check(
                queries == 2 && after.instructions.size() == before.instructions.size() + 6,
                "receiver gains exactly six instructions");
        Map<Integer, Integer> oldMap = new HashMap<>(), newMap = new HashMap<>();

        for (int i = 0; i < before.instructions.size(); i++) {
            oldMap.put(i, i);
        }

        for (int i = 0, retained = 0; i < after.instructions.size(); i++) {
            if (!added.contains(i)) {
                newMap.put(i, retained++);
            }
        }

        for (var entry : newMap.entrySet()) {
            check(
                    signature(before, entry.getValue(), oldMap)
                            .equals(signature(after, entry.getKey(), newMap)),
                    "all original connectivity receiver instructions/control flow retained");
        }

        for (int q = 0; q < 2; q++) {
            int at = added.get(q * 3);
            var present = (OffsetInstruction) after.instructions.get(at);
            var absent = (OffsetInstruction) after.instructions.get(at + 2);
            int presentTarget = after.index.get(after.addresses.get(at) + present.getCodeOffset()),
                    absentTarget =
                            after.index.get(after.addresses.get(at + 2) + absent.getCodeOffset());
            check(presentTarget == at + 3, "present transport still invokes original getState");
            check(
                    newMap.get(absentTarget).equals(before.index.get(afterAddresses[q])),
                    "absent transport continues existing OR/log/disconnected logic");
        }
    }

    interface Checked {
        void run() throws Exception;
    }

    static void reject(Checked action) throws Exception {
        boolean failed = false;

        try {
            action.run();
        } catch (Failure expected) {
            check(expected.code.equals("NETWORK_CONTRACT"), "precise guard failure");
            failed = true;
        }

        check(failed, "unsupported owner/profile/API/Dex or reapplication must be rejected");
    }

    public static void main(String[] args) throws Exception {
        check(args.length == 1 || args.length == 2, "ROOT [PREPARED_SYSTEM_SERVICE_APK]");
        Path root = Path.of(args[0]).toRealPath(),
                original =
                        root.resolve(".local/profiles/" + PROFILE + "/originals/SystemService.apk");
        check(
                FilesSupport.hash(original).equals(PIN),
                "exact original A11 SystemService owner pin");
        byte[] before;

        try (var zip = new java.util.zip.ZipFile(original.toFile())) {
            before = zip.getInputStream(zip.getEntry("classes.dex")).readAllBytes();
        }

        byte[] patched = NetworkGuard.apply(PROFILE, 30, original, before);
        verify(
                method(original),
                method(
                        new org.jf.dexlib2.dexbacked.DexBackedDexFile(
                                org.jf.dexlib2.Opcodes.forApi(30), patched)));
        verifyReceiver(
                receiver(
                        new org.jf.dexlib2.dexbacked.DexBackedDexFile(
                                org.jf.dexlib2.Opcodes.forApi(30), before)),
                receiver(
                        new org.jf.dexlib2.dexbacked.DexBackedDexFile(
                                org.jf.dexlib2.Opcodes.forApi(30), patched)));
        reject(() -> NetworkGuard.validate("other-profile", 30, original, before));
        reject(() -> NetworkGuard.validate(PROFILE, 28, original, before));
        reject(() -> NetworkGuard.validate(PROFILE, 30, original, null));
        reject(() -> NetworkGuard.validate(PROFILE, 30, original, patched));
        reject(() ->
                NetworkGuard.validate(
                        PROFILE,
                        30,
                        root.resolve(
                                ".local/profiles/" + PROFILE + "/originals/framework/QGAPI.jar"),
                        before));

        if (args.length == 2) {
            verify(method(original), method(Path.of(args[1]).toRealPath()));
            verifyReceiver(
                    receiver(DexOperations.dex(original, "classes.dex", 30)),
                    receiver(DexOperations.dex(Path.of(args[1]).toRealPath(), "classes.dex", 30)));
        }

        check(FilesSupport.hash(original).equals(PIN), "original owner input unchanged");
        System.out.println(
                "Actual owner network queries and connectivity receiver preserve OR/disconnected"
                        + " control flow and all retained instructions, reject six unsupported"
                        + " mutations; no guest acceptance");
    }
}
