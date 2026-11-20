package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableExceptionHandler;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.ImmutableTryBlock;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction35c;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/** Preserve the OEM disconnected/error path when this unavailable audio operation throws. */
final class SettingsVoiceMuteGuard {
    static final String NAME = "api30-settings-voice-mute";
    static final String APK = "6a902c0458c68b66317fa666f7ee783d38d028392a3c114d1ae3675bdedc2c03";
    static final String OWNER = "Lcom/qinggan/audiopolicy/AudioPolicyManager;",
            REMOTE = "Landroid/os/RemoteException;",
            UOE = "Ljava/lang/UnsupportedOperationException;",
            THROWABLE = "Ljava/lang/Throwable;";

    static void need(boolean value) {
        Failure.require(
                value,
                "SETTINGS_VOICE_MUTE",
                "Exact Settings AudioPolicyManager voice mute boundary required",
                3);
    }

    static boolean target(Method m) {
        return OWNER.equals(m.getDefiningClass())
                && m.getName().equals("setVoiceMute")
                && m.getParameterTypes().equals(List.of("Z"))
                && m.getReturnType().equals("V");
    }

    static Map<String, ClassDef> classes(byte[] dex) {
        var result = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            need(result.put(c.getType(), c) == null);
        }

        return result;
    }

    static byte[] original(Path apk) throws Exception {
        try (var zip = new java.util.zip.ZipFile(apk.toFile())) {
            var e = zip.getEntry("classes.dex");
            need(e != null);

            return zip.getInputStream(e).readAllBytes();
        }
    }

    static void requireBinderCall(Instruction ins) {
        need(
                ins.getOpcode() == Opcode.INVOKE_INTERFACE
                        && ins instanceof ReferenceInstruction
                        && ins instanceof FiveRegisterInstruction);
        var r = (FiveRegisterInstruction) ins;
        need(
                r.getRegisterCount() == 2
                        && r.getRegisterC() == 1
                        && r.getRegisterD() == 4
                        && ((ReferenceInstruction) ins)
                                .getReference()
                                .toString()
                                .equals(
                                        "Lcom/qinggan/audiopolicy/IAudioPolicyService;->setVoiceMute(Z)V"));
    }

    static Instruction rewriteMessage(Instruction ins, boolean inverse) {
        need(
                ins.getOpcode() == Opcode.INVOKE_VIRTUAL
                        && ins instanceof ReferenceInstruction
                        && ins instanceof FiveRegisterInstruction);
        var r = (FiveRegisterInstruction) ins;
        need(
                r.getRegisterCount() == 1
                        && r.getRegisterC() == 4
                        && ((ReferenceInstruction) ins)
                                .getReference()
                                .toString()
                                .equals(
                                        (inverse ? THROWABLE : REMOTE)
                                                + "->getMessage()Ljava/lang/String;"));

        return new ImmutableInstruction35c(
                Opcode.INVOKE_VIRTUAL,
                1,
                4,
                0,
                0,
                0,
                0,
                new ImmutableMethodReference(
                        inverse ? REMOTE : THROWABLE,
                        "getMessage",
                        List.of(),
                        "Ljava/lang/String;"));
    }

    static Method rewrite(Method m, boolean inverse) {
        var body = m.getImplementation();

        need(
                body != null
                        && m.getAccessFlags() == 1
                        && body.getRegisterCount() == 5
                        && body.getTryBlocks().size() == 1);
        var old = body.getTryBlocks().get(0);
        need(old.getStartCodeAddress() == 2 && old.getCodeUnitCount() == 18);
        var handlers = old.getExceptionHandlers();
        need(
                handlers.size() == (inverse ? 2 : 1)
                        && REMOTE.equals(handlers.get(0).getExceptionType())
                        && handlers.get(0).getHandlerCodeAddress() == 0x15);

        if (inverse) {
            need(
                    UOE.equals(handlers.get(1).getExceptionType())
                            && handlers.get(1).getHandlerCodeAddress() == 0x15);
        }

        var code = new ArrayList<Instruction>();
        int address = 0, binder = 0, message = 0;

        for (var ins : body.getInstructions()) {
            if (address == 8) {
                requireBinderCall(ins);
                binder++;
            }

            if (address == 0x21) {
                code.add(rewriteMessage(ins, inverse));
                message++;
            } else {
                code.add(ins);
            }

            address += ins.getCodeUnits();
        }

        need(binder == 1 && message == 1 && address == 0x33);
        var next = new ArrayList<ImmutableExceptionHandler>();
        next.add(new ImmutableExceptionHandler(REMOTE, 0x15));

        if (!inverse) {
            next.add(new ImmutableExceptionHandler(UOE, 0x15));
        }

        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                new ImmutableMethodImplementation(
                        body.getRegisterCount(),
                        code,
                        List.of(new ImmutableTryBlock(2, 18, next)),
                        body.getDebugItems()));
    }

    static Map<String, ClassDef> transform(Map<String, ClassDef> input, boolean inverse) {
        var result = new TreeMap<String, ClassDef>();
        int count = 0;

        for (var c : input.values()) {
            if (!OWNER.equals(c.getType())) {
                result.put(c.getType(), c);
                continue;
            }

            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();
            int group = 0;

            for (var methods : List.of(c.getDirectMethods(), c.getVirtualMethods())) {
                var dest = group++ == 0 ? direct : virtual;

                for (var m : methods) {
                    if (target(m)) {
                        dest.add(rewrite(m, inverse));
                        count++;
                    } else {
                        dest.add(m);
                    }
                }
            }

            result.put(
                    c.getType(),
                    new ImmutableClassDef(
                            c.getType(),
                            c.getAccessFlags(),
                            c.getSuperclass(),
                            c.getInterfaces(),
                            c.getSourceFile(),
                            c.getAnnotations(),
                            c.getStaticFields(),
                            c.getInstanceFields(),
                            direct,
                            virtual));
        }

        need(count == 1);

        return result;
    }

    static void validate(Map<String, Object> constants, int api, Path apk, byte[] dex)
            throws Exception {
        need(api == 30 && APK.equals(FilesSupport.hash(apk)) && dex != null);
        transform(classes(dex), false);
    }

    static void validateOriginalChain(Map<String, Object> constants, int api, Path apk, byte[] raw)
            throws Exception {
        need(
                api == 30
                        && APK.equals(FilesSupport.hash(apk))
                        && raw != null
                        && Arrays.equals(raw, original(apk)));
        validate(constants, api, apk, SettingsVoiceCloneGuard.apply(constants, api, apk, raw));
    }

    static byte[] apply(Map<String, Object> constants, int api, Path apk, byte[] dex)
            throws Exception {
        validate(constants, api, apk, dex);
        var before = classes(dex);
        var expected = transform(before, false);
        byte[] output =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), expected.values()));
        var actual = classes(output);
        need(expected.keySet().equals(actual.keySet()));

        for (var name : expected.keySet()) {
            need(
                    FrameworkDexMaterializer.semantic(expected.get(name))
                            .equals(FrameworkDexMaterializer.semantic(actual.get(name))));
        }

        var restored = transform(actual, true);

        for (var name : before.keySet()) {
            need(
                    FrameworkDexMaterializer.semantic(before.get(name))
                            .equals(FrameworkDexMaterializer.semantic(restored.get(name))));
        }

        return output;
    }
}
