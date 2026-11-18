package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.zip.ZipFile;

public class SettingsVoiceCloneBranchTest {
    static final String PROFILE = "free-11-chinese-20250710-6.11.1",
            OTHER = "free-11-chinese-20260731-7.3.1";

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
                    if (m.getName().equals(name) && m.getImplementation() != null) {
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
        byte[] d = dex(apk, "classes.dex");

        SettingsVoiceCloneGuard.validate(PROFILE, 30, apk, d);
        denied(() -> SettingsVoiceCloneGuard.validate(OTHER, 30, apk, d));
        denied(() -> SettingsVoiceCloneGuard.validate(PROFILE, 30, other, d));
        byte[] out = SettingsVoiceCloneGuard.apply(PROFILE, 30, apk, d);
        denied(() -> SettingsVoiceCloneGuard.validate(PROFILE, 30, apk, out));

        for (String name : new String[] {"initVoiceClone", "showView"}) {
            var ins = instructions(method(out, SettingsVoiceCloneGuard.OWNER, name));
            int resolve = -1;

            for (int i = 0; i < ins.size(); i++) {
                if (ins.get(i) instanceof org.jf.dexlib2.iface.instruction.ReferenceInstruction r
                        && r.getReference()
                                .toString()
                                .equals(
                                        "Landroid/content/pm/PackageManager;->resolveContentProvider(Ljava/lang/String;I)Landroid/content/pm/ProviderInfo;")) {
                    resolve = i;
                }
            }

            check(
                    resolve >= 0
                            && ins.get(resolve + 1).getOpcode()
                                    == org.jf.dexlib2.Opcode.MOVE_RESULT_OBJECT
                            && ins.get(resolve + 2).getOpcode() == org.jf.dexlib2.Opcode.IF_NEZ,
                    "provider branch " + name);

            for (String field :
                    new String[] {"radioMyVoice", "myVoiceTips", "voiceSelectRecordVoice"}) {
                check(
                        ins.stream()
                                .anyMatch(i ->
                                        i
                                                        instanceof
                                                        org.jf.dexlib2.iface.instruction
                                                                        .ReferenceInstruction
                                                                r
                                                && r.getReference()
                                                        .toString()
                                                        .contains("->" + field + ":")),
                        "hide field " + field);
            }

            check(
                    refs(ins, "Landroid/view/View;->setVisibility(I)V") == 3,
                    "three hide calls " + name);

            if (name.equals("initVoiceClone")) {
                check(
                        ins.get(resolve + 13).getOpcode() == org.jf.dexlib2.Opcode.RETURN_VOID,
                        "absent provider returns");
            }
        }
    }
}
