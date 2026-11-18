package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;
import org.jf.dexlib2.iface.instruction.formats.Instruction35c;

import java.nio.file.Path;
import java.util.List;
import java.util.Objects;
import java.util.TreeMap;

/** Executes the real A9 query's nullable transport branches and preserves its receiver logic. */
public final class A9NetworkNullGuardTest {
    static final String PROFILE = "free-09-chinese-20260106-6.0.3";
    static final String PIN = "2a62d362cf27bc85a713a0785f5ec426c31a52b19b42c6ce54e55f3e71de5655";

    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private record Link(boolean connected) {}

    /** Small bounded executor of the actual private OEM query, not a Java reimplementation. */
    private static boolean query(Method method, Link ethernet, Link wifi) {
        var body = new NetworkGuardTest.Body(method);
        Object[] registers = new Object[method.getImplementation().getRegisterCount()];
        Object result = null;
        int index = 0;

        for (int step = 0; step < 80; step++) {
            var instruction = body.instructions().get(index);
            int next = index + 1;

            switch (instruction.getOpcode()) {
                case CONST_4, CONST_16 ->
                        registers[((OneRegisterInstruction) instruction).getRegisterA()] =
                                ((NarrowLiteralInstruction) instruction).getNarrowLiteral();
                case IGET_OBJECT ->
                        registers[((TwoRegisterInstruction) instruction).getRegisterA()] =
                                "manager";
                case INVOKE_VIRTUAL -> {
                    var invoke = (Instruction35c) instruction;
                    String reference =
                            ((ReferenceInstruction) instruction).getReference().toString();

                    if (reference.equals(NetworkGuard.QUERY)) {
                        int type = (Integer) registers[invoke.getRegisterD()];
                        check(type == 1 || type == 9, "Only original WiFi/Ethernet queries");
                        result = type == 9 ? ethernet : wifi;
                    } else {
                        check(
                                reference.equals(
                                        "Landroid/net/NetworkInfo;->getState()Landroid/net/NetworkInfo$State;"),
                                "Only original NetworkInfo state query");
                        Link value =
                                Objects.requireNonNull((Link) registers[invoke.getRegisterC()]);
                        result = value.connected() ? "CONNECTED" : "DISCONNECTED";
                    }
                }
                case MOVE_RESULT_OBJECT ->
                        registers[((OneRegisterInstruction) instruction).getRegisterA()] = result;
                case SGET_OBJECT -> {
                    check(
                            ((ReferenceInstruction) instruction)
                                    .getReference()
                                    .toString()
                                    .equals(
                                            "Landroid/net/NetworkInfo$State;->CONNECTED:Landroid/net/NetworkInfo$State;"),
                            "Original connected constant");
                    registers[((OneRegisterInstruction) instruction).getRegisterA()] = "CONNECTED";
                }
                case IF_EQZ -> {
                    Object value = registers[((OneRegisterInstruction) instruction).getRegisterA()];

                    if (value == null || value.equals(0)) {
                        next =
                                body.index()
                                        .get(
                                                body.addresses().get(index)
                                                        + ((OffsetInstruction) instruction)
                                                                .getCodeOffset());
                    }
                }
                case IF_EQ, IF_NE -> {
                    var operands = (TwoRegisterInstruction) instruction;
                    boolean equal =
                            Objects.equals(
                                    registers[operands.getRegisterA()],
                                    registers[operands.getRegisterB()]);

                    if (equal == (instruction.getOpcode() == org.jf.dexlib2.Opcode.IF_EQ)) {
                        next =
                                body.index()
                                        .get(
                                                body.addresses().get(index)
                                                        + ((OffsetInstruction) instruction)
                                                                .getCodeOffset());
                    }
                }
                case MOVE -> {
                    var operands = (TwoRegisterInstruction) instruction;
                    registers[operands.getRegisterA()] = registers[operands.getRegisterB()];
                }
                case RETURN -> {
                    return !registers[((OneRegisterInstruction) instruction).getRegisterA()].equals(
                            0);
                }
                default ->
                        throw new AssertionError(
                                "Unexpected OEM query opcode " + instruction.getOpcode());
            }

            index = next;
        }

        throw new AssertionError("OEM query exceeded bounded instruction budget");
    }

    private static DexBackedDexFile dex(byte[] bytes) {
        return new DexBackedDexFile(Opcodes.forApi(28), bytes);
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]).toRealPath();
        Path original = root.resolve(".local/profiles/" + PROFILE + "/originals/SystemService.apk");

        check(FilesSupport.hash(original).equals(PIN), "Exact original A9 SystemService");
        var archive = new TreeMap<>(NativeCore.entries(original));
        byte[] before = archive.get("classes.dex");
        Method originalQuery = NetworkGuardTest.method(dex(before));
        boolean reproduced = false;

        try {
            query(originalQuery, null, new Link(true));
        } catch (NullPointerException expected) {
            reproduced = true;
        }

        check(reproduced, "Original absent-Ethernet crash reproduced");
        byte[] after = NetworkGuard.apply(PROFILE, 28, original, before);
        Method patchedQuery = NetworkGuardTest.method(dex(after));
        NetworkGuardTest.verify(originalQuery, patchedQuery);
        NetworkGuardTest.verifyReceiver(
                NetworkGuardTest.receiver(dex(before)), NetworkGuardTest.receiver(dex(after)));
        Link[] states = {null, new Link(false), new Link(true)};

        for (Link ethernet : states) {
            for (Link wifi : states) {
                boolean expected =
                        (ethernet != null && ethernet.connected())
                                || (wifi != null && wifi.connected());
                check(
                        query(patchedQuery, ethernet, wifi) == expected,
                        "Nullable transports retain original connected OR semantics");
            }
        }

        NetworkGuardTest.reject(() -> NetworkGuard.validate(PROFILE, 30, original, before));
        NetworkGuardTest.reject(() -> NetworkGuard.validate("other-profile", 28, original, before));
        NetworkGuardTest.reject(() -> NetworkGuard.validate(PROFILE, 28, original, after));
        NetworkGuardTest.reject(() -> NetworkGuard.validate(PROFILE, 28, original, null));
        var classes = new DexClasses(archive, List.of("classes.dex"), 28);
        NetworkGuard.apply(PROFILE, 28, PIN, classes);
        classes.write(archive);
        NetworkGuardTest.verify(
                originalQuery, NetworkGuardTest.method(dex(archive.get("classes.dex"))));
        NetworkGuardTest.reject(() -> NetworkGuard.apply(PROFILE, 28, PIN, classes));
        check(FilesSupport.hash(original).equals(PIN), "Original input unchanged");
        System.out.println(
                "A9 original null Ethernet crash reproduced; nine nullable transport states, real"
                        + " receiver, API28 typed pipeline and rejection guards passed");
    }
}
