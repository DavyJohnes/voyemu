package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10x;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;

import java.nio.file.Path;
import java.util.ArrayList;

public final class SettingsVoiceMuteGuardTest {
    interface Check {
        void run() throws Exception;
    }

    static void need(boolean b, String message) {
        if (!b) {
            throw new AssertionError(message);
        }
    }

    static void reject(Check c) throws Exception {
        try {
            c.run();
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError("guard accepted wrong input");
    }

    static Method target(byte[] dex) {
        Method found = null;

        for (var c : SettingsVoiceMuteGuard.classes(dex).values()) {
            for (var m : c.getMethods()) {
                if (SettingsVoiceMuteGuard.target(m)) {
                    need(found == null, "duplicate target");
                    found = m;
                }
            }
        }

        need(found != null, "target absent");

        return found;
    }

    static boolean catches(Method m, String type, int pc) {
        for (var t : m.getImplementation().getTryBlocks()) {
            if (pc >= t.getStartCodeAddress()
                    && pc < t.getStartCodeAddress() + t.getCodeUnitCount()) {
                for (var h : t.getExceptionHandlers()) {
                    if (type.equals(h.getExceptionType()) && h.getHandlerCodeAddress() == 0x15) {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    static String messageOwner(Method m) {
        int at = 0;

        for (var i : m.getImplementation().getInstructions()) {
            if (at == 0x21) {
                return ((ReferenceInstruction) i).getReference().toString();
            }

            at += i.getCodeUnits();
        }

        throw new AssertionError("message missing");
    }

    static byte[] unrelated(byte[] source) throws Exception {
        var cs = SettingsVoiceMuteGuard.classes(source);
        var owner = cs.get(SettingsVoiceCloneGuard.OWNER);

        need(owner != null, "voice clone owner missing");
        var direct = new ArrayList<Method>();
        int n = 0;

        for (var m : owner.getDirectMethods()) {
            if (m.getName().equals("initVoiceClone")) {
                var body = new MutableMethodImplementation(m.getImplementation());
                body.addInstruction(0, new BuilderInstruction10x(Opcode.NOP));
                direct.add(PassionServiceForegroundGuard.copy(m, body));
                n++;
            } else {
                direct.add(m);
            }
        }

        need(n == 1, "fixture target");
        cs.put(
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

        return FrameworkDexMaterializer.serialize(
                new ImmutableDexFile(Opcodes.forApi(30), cs.values()));
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]);
        Path apk =
                root.resolve(
                        ".local/profiles/"
                                + SettingsVoiceMuteGuard.PROFILE
                                + "/originals/Setting.apk");
        Path other =
                root.resolve(
                        ".local/profiles/free-11-chinese-20260731-7.3.1/originals/Setting.apk");
        byte[] raw = NativeCore.entries(apk).get("classes.dex");
        String profile = SettingsVoiceMuteGuard.PROFILE;

        SettingsVoiceMuteGuard.validateOriginalChain(profile, 30, apk, raw);
        byte[] predecessor = SettingsVoiceCloneGuard.apply(profile, 30, apk, raw);
        byte[] output = SettingsVoiceMuteGuard.apply(profile, 30, apk, predecessor);
        Method before = target(predecessor), after = target(output);
        need(
                catches(before, SettingsVoiceMuteGuard.REMOTE, 8)
                        && !catches(before, SettingsVoiceMuteGuard.UOE, 8),
                "original reproduces uncaught unsupported operation");
        need(
                catches(after, SettingsVoiceMuteGuard.REMOTE, 8)
                        && catches(after, SettingsVoiceMuteGuard.UOE, 8)
                        && !catches(after, "Ljava/lang/SecurityException;", 8),
                "only intended exception added");
        need(
                messageOwner(after).equals("Ljava/lang/Throwable;->getMessage()Ljava/lang/String;"),
                "shared handler must verify for both exception types");
        byte[] bad = unrelated(predecessor);
        ViewDumpPatch.checkedDex(bad, NativeCore.hash(bad));
        need(
                FrameworkDexMaterializer.semantic(target(predecessor))
                        .equals(FrameworkDexMaterializer.semantic(target(bad))),
                "unrelated fixture changed target");
        reject(() -> SettingsVoiceMuteGuard.apply(profile, 30, other, predecessor));
        reject(() ->
                SettingsVoiceMuteGuard.apply(
                        "free-11-chinese-20260731-7.3.1", 30, apk, predecessor));
        reject(() -> SettingsVoiceMuteGuard.apply(profile, 30, apk, output));
    }
}
