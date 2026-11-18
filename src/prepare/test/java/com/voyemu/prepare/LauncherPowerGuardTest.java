package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.zip.ZipFile;

public class LauncherPowerGuardTest {
    static final String OTHER = "free-11-chinese-20260731-7.3.1";

    static final String PROFILE = "free-11-chinese-20250710-6.11.1";

    interface Checked {
        void run() throws Exception;
    }

    static void denied(Checked c) throws Exception {
        try {
            c.run();
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError("invalid preimage accepted");
    }

    static byte[] dex(Path apk, String name) throws Exception {
        try (var z = new ZipFile(apk.toFile())) {
            return z.getInputStream(z.getEntry(name)).readAllBytes();
        }
    }

    static org.jf.dexlib2.iface.Method method(byte[] dex, String owner, String name) {
        for (var c :
                new org.jf.dexlib2.dexbacked.DexBackedDexFile(
                                org.jf.dexlib2.Opcodes.forApi(30), dex)
                        .getClasses()) {
            if (c.getType().equals(owner)) {
                for (var m : c.getMethods()) {
                    if (m.getName().equals(name)
                            && m.getParameterTypes().isEmpty()
                            && m.getImplementation() != null) {
                        return m;
                    }
                }
            }
        }

        throw new AssertionError("method missing: " + owner + " " + name);
    }

    static java.util.List<org.jf.dexlib2.iface.instruction.Instruction> instructions(
            org.jf.dexlib2.iface.Method m) {
        var out = new java.util.ArrayList<org.jf.dexlib2.iface.instruction.Instruction>();

        m.getImplementation().getInstructions().forEach(out::add);

        return out;
    }

    static long refs(java.util.List<org.jf.dexlib2.iface.instruction.Instruction> ins, String ref) {
        return ins.stream()
                .filter(i ->
                        i
                                        instanceof
                                        org.jf.dexlib2.iface.instruction.ReferenceInstruction
                                                referenceInstruction
                                && referenceInstruction.getReference().toString().equals(ref))
                .count();
    }

    static void check(boolean b, String why) {
        if (!b) {
            throw new AssertionError(why);
        }
    }

    public static void main(String[] args) throws Exception {
        Path apk = Path.of(args[0]), other = Path.of(args[1]);

        for (String entry : new String[] {"classes.dex", "classes2.dex"}) {
            byte[] d = dex(apk, entry);
            LauncherPowerGuard.validate(PROFILE, 30, apk, d, entry);
            denied(() -> LauncherPowerGuard.validate(OTHER, 30, apk, d, entry));
            denied(() -> LauncherPowerGuard.validate(PROFILE, 30, other, d, entry));
            byte[] out = LauncherPowerGuard.apply(PROFILE, 30, apk, d, entry);
            denied(() -> LauncherPowerGuard.validate(PROFILE, 30, apk, out, entry));
            boolean wrapper = entry.equals("classes2.dex");
            var ins =
                    instructions(
                            method(
                                    out,
                                    wrapper
                                            ? LauncherPowerGuard.POWER_OWNER
                                            : LauncherPowerGuard.MAP_OWNER,
                                    wrapper ? "<init>" : "init"));
            int checkAt = -1;

            for (int i = 0; i < ins.size(); i++) {
                if (ins.get(i) instanceof org.jf.dexlib2.iface.instruction.ReferenceInstruction r
                        && r.getReference()
                                .toString()
                                .equals(
                                        "Landroid/os/ServiceManager;->checkService(Ljava/lang/String;)Landroid/os/IBinder;")) {
                    checkAt = i;
                }
            }

            check(
                    checkAt > 0
                            && ins.get(checkAt - 1).getOpcode()
                                    == org.jf.dexlib2.Opcode.CONST_STRING
                            && ((org.jf.dexlib2.iface.reference.StringReference)
                                            ((org.jf.dexlib2.iface.instruction.ReferenceInstruction)
                                                            ins.get(checkAt - 1))
                                                    .getReference())
                                    .getString()
                                    .equals("car_service")
                            && ins.get(checkAt + 1).getOpcode()
                                    == org.jf.dexlib2.Opcode.MOVE_RESULT_OBJECT
                            && ins.get(checkAt + 2).getOpcode() == org.jf.dexlib2.Opcode.IF_EQZ,
                    "service gate " + entry);
        }
    }
}
