package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.BuilderInstruction;
import org.jf.dexlib2.builder.Label;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11n;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21s;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableFieldReference;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;
import org.jf.dexlib2.immutable.reference.ImmutableStringReference;

import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

final class SettingsVoiceCloneGuard {
    static final String OWNER = "Lcom/qinggan/app/setting/module/voice/VoiceFragment;",
            BIND = "Lcom/qinggan/app/setting/databinding/FragmentVoiceBinding;",
            AUTH = "com.qinggan.qingai.clone.voice";
    static final String NAME = "api30-settings-voice-clone";

    /**
     * The step constant settings_voice_clone_boundary selects the instruction-boundary check;
     * sound_balance adds the sound balance guard and the versionCode manifest step to the same
     * patch.
     */
    static boolean soundBalance(Map<String, Object> constants) {
        return StepConstants.flag(constants, "sound_balance");
    }

    static String hash(byte[] b) throws Exception {
        return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(b));
    }

    static void check(boolean b) {
        Failure.require(
                b,
                "SETTINGS_VOICE_CLONE",
                "Exact optional VoiceClone provider boundary required",
                3);
    }

    static boolean target(Method m) {
        return m.getDefiningClass().equals(OWNER)
                && ((m.getName().equals("initVoiceClone") && m.getParameterTypes().isEmpty())
                        || (m.getName().equals("showView")
                                && m.getParameterTypes().equals(List.of("Ljava/lang/String;"))))
                && m.getReturnType().equals("V");
    }

    static ImmutableMethodReference call(String owner, String name, List<String> args, String ret) {
        return new ImmutableMethodReference(owner, name, args, ret);
    }

    static List<BuilderInstruction> availability() {
        return List.of(
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        call(
                                "Lcom/qinggan/app/setting/componement/SettingApp;",
                                "getContext",
                                List.of(),
                                "Landroid/content/Context;")),
                new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0),
                new BuilderInstruction35c(
                        Opcode.INVOKE_VIRTUAL,
                        1,
                        0,
                        0,
                        0,
                        0,
                        0,
                        call(
                                "Landroid/content/Context;",
                                "getPackageManager",
                                List.of(),
                                "Landroid/content/pm/PackageManager;")),
                new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0),
                new BuilderInstruction21c(
                        Opcode.CONST_STRING, 1, new ImmutableStringReference(AUTH)),
                new BuilderInstruction11n(Opcode.CONST_4, 2, 0),
                new BuilderInstruction35c(
                        Opcode.INVOKE_VIRTUAL,
                        3,
                        0,
                        1,
                        2,
                        0,
                        0,
                        call(
                                "Landroid/content/pm/PackageManager;",
                                "resolveContentProvider",
                                List.of("Ljava/lang/String;", "I"),
                                "Landroid/content/pm/ProviderInfo;")),
                new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0));
    }

    static List<BuilderInstruction> hide(int self) {
        var out = new ArrayList<BuilderInstruction>();

        out.add(new BuilderInstruction21s(Opcode.CONST_16, 1, 8));

        for (var f :
                List.of(
                        List.of("radioMyVoice", "Lcom/pateo/material/widgets/BoldRadioButton;"),
                        List.of("myVoiceTips", "Landroid/widget/TextView;"),
                        List.of("voiceSelectRecordVoice", "Landroid/widget/LinearLayout;"))) {
            out.add(
                    new BuilderInstruction22c(
                            Opcode.IGET_OBJECT,
                            0,
                            self,
                            new ImmutableFieldReference(OWNER, "binding", BIND)));
            out.add(
                    new BuilderInstruction22c(
                            Opcode.IGET_OBJECT,
                            0,
                            0,
                            new ImmutableFieldReference(BIND, f.get(0), f.get(1))));
            out.add(
                    new BuilderInstruction35c(
                            Opcode.INVOKE_VIRTUAL,
                            2,
                            0,
                            1,
                            0,
                            0,
                            0,
                            call("Landroid/view/View;", "setVisibility", List.of("I"), "V")));
        }

        return out;
    }

    static Method patch(Method m) {
        var body = new MutableMethodImplementation(m.getImplementation());
        int self = m.getImplementation().getRegisterCount() - 1 - m.getParameterTypes().size();
        boolean init = m.getName().equals("initVoiceClone");
        int index = init ? 3 : body.getInstructions().size() - 1;
        int addr = 0;

        for (int i = 0; i < index; i++) {
            addr += body.getInstructions().get(i).getCodeUnits();
        }

        var originalLabels =
                new ArrayList<Label>(body.getInstructions().get(index).getLocation().getLabels());
        var resume = body.newLabelForAddress(addr);
        var xs = new ArrayList<BuilderInstruction>();
        xs.addAll(availability());
        xs.add(new BuilderInstruction21t(Opcode.IF_NEZ, 0, resume));
        xs.addAll(hide(self));

        if (init) {
            xs.add(new BuilderInstruction10x(Opcode.RETURN_VOID));
        }

        for (int i = 0; i < xs.size(); i++) {
            body.addInstruction(index + i, xs.get(i));
        } /* Existing branches into the shared return must pass the availability guard. */

        if (!init) {
            var old = body.getInstructions().get(index + xs.size()).getLocation();
            var entry = body.getInstructions().get(index).getLocation();

            for (var label : originalLabels) {
                old.getLabels().remove(label);
                entry.getLabels().add(label);
            }
        }

        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                body);
    }

    static void insertion(Method m) {
        var ins = new ArrayList<org.jf.dexlib2.iface.instruction.Instruction>();

        m.getImplementation().getInstructions().forEach(ins::add);
        check(!ins.isEmpty() && ins.get(ins.size() - 1).getOpcode() == Opcode.RETURN_VOID);

        if (m.getName().equals("initVoiceClone")) {
            check(
                    ins.size() > 3
                            && ins.get(0).getOpcode() == Opcode.INVOKE_VIRTUAL
                            && ins.get(1).getOpcode() == Opcode.MOVE_RESULT_OBJECT
                            && ins.get(2).getOpcode() == Opcode.IF_EQZ
                            && ins.get(3).getOpcode() == Opcode.NEW_INSTANCE);
            var first = (org.jf.dexlib2.iface.instruction.ReferenceInstruction) ins.get(0);
            var next = (org.jf.dexlib2.iface.instruction.ReferenceInstruction) ins.get(3);
            check(
                    first.getReference()
                                    .toString()
                                    .equals(
                                            "Lcom/qinggan/app/setting/module/voice/VoiceFragment;->getActivity()Landroidx/fragment/app/FragmentActivity;")
                            && next.getReference()
                                    .toString()
                                    .equals("Lcom/qinggan/app/setting/utils/VoiceCloneHelper;"));
        }
    }

    static void validate(Map<String, Object> constants, int api, Path apk, byte[] dex)
            throws Exception {
        boolean boundary = StepConstants.flag(constants, "settings_voice_clone_boundary");

        check(
                api == 30
                        && StepConstants.admits(constants, hash(Files.readAllBytes(apk)))
                        && dex != null);
        int count = 0;

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            for (var m : c.getDirectMethods()) {
                if (target(m)) {
                    check(
                            m.getImplementation() != null
                                    && m.getAccessFlags() == 2
                                    && m.getImplementation().getTryBlocks().isEmpty());

                    if (boundary) {
                        insertion(m);
                    }

                    count++;
                }
            }
        }

        check(count == 2);
    }

    static byte[] apply(Map<String, Object> constants, int api, Path apk, byte[] dex)
            throws Exception {
        validate(constants, api, apk, dex);
        check(api == 30 && StepConstants.admits(constants, hash(Files.readAllBytes(apk))));
        var all = new TreeMap<String, ClassDef>();
        int n = 0;

        for (ClassDef c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            if (c.getType().equals(OWNER)) {
                var direct = new ArrayList<Method>();

                for (var m : c.getDirectMethods()) {
                    if (target(m)) {
                        direct.add(patch(m));
                        n++;
                    } else {
                        direct.add(m);
                    }
                }

                c =
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
                                c.getVirtualMethods());
            }

            all.put(c.getType(), c);
        }

        check(n == 2);

        return FrameworkDexMaterializer.serialize(
                new ImmutableDexFile(Opcodes.forApi(30), all.values()));
    }
}
