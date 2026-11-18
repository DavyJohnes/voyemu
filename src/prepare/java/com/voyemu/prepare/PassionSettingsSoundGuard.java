package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.debug.DebugItem;
import org.jf.dexlib2.iface.debug.EndLocal;
import org.jf.dexlib2.iface.debug.RestartLocal;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.debug.ImmutableEndLocal;
import org.jf.dexlib2.immutable.debug.ImmutableRestartLocal;
import org.jf.dexlib2.immutable.reference.ImmutableFieldReference;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;
import org.jf.dexlib2.immutable.reference.ImmutableTypeReference;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;

/** OEM CarAudio path stays intact; null Car gets real Android stream controls. */
final class PassionSettingsSoundGuard {
    static final String PROFILE = "passion-11-russian-20260403-2.1.4.80",
            APK = "8d7798dd439ffa39d8af491ff23458d321c8c7659b53ea3672ab4d199189e4b4",
            OWNER = "Lcom/android/car/settings/sound/VolumeSettingsPreferenceController;";
    static final String NAME = "passion-api30-settings-sound";
    static final Set<String> SOURCES =
            Set.of(
                    "com/voyemu/settings/PassionSoundStreams.java",
                    "compile-only/androidx/preference/Preference.java",
                    "compile-only/androidx/preference/PreferenceGroup.java",
                    "compile-only/com/android/car/settings/common/SeekBarPreference.java");
    static final Set<String> METHODS =
            Set.of("onCreateInternal", "onDestroyInternal", "updateState");

    static void need(boolean b) {
        Failure.require(b, "PASSION_SOUND_CONTRACT", "Exact Passion Sound controller required", 3);
    }

    static boolean target(Method m) {
        return m.getDefiningClass().equals(OWNER)
                && METHODS.contains(m.getName())
                && (m.getName().equals("updateState")
                        ? m.getParameterTypes()
                                .equals(List.of("Landroidx/preference/PreferenceGroup;"))
                        : m.getParameterTypes().isEmpty())
                && m.getReturnType().equals("V");
    }

    static void validate(Method m) throws Exception {
        need(
                target(m)
                        && m.getAccessFlags() == 4
                        && m.getImplementation() != null
                        && m.getImplementation().getRegisterCount()
                                == (m.getName().equals("updateState") ? 4 : 2)
                        && m.getImplementation().getTryBlocks().isEmpty());
    }

    static void validate(String profile, int api, Path apk, byte[] dex) throws Exception {
        need(
                PROFILE.equals(profile)
                        && api == 30
                        && FilesSupport.hash(apk).equals(APK)
                        && dex != null);
        int count = 0;

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            for (var m : c.getVirtualMethods()) {
                if (target(m)) {
                    validate(m);
                    count++;
                }
            }
        }

        need(count == 3);
    }

    static void validateSource(Path source) throws Exception {
        var tree = FilesSupport.tree(source);

        need(tree.keySet().equals(SOURCES));
    }

    static Path compileHelper(
            String profile, int api, Path apk, Path source, Path platform, Path work)
            throws Exception {
        need(PROFILE.equals(profile) && api == 30 && FilesSupport.hash(apk).equals(APK));
        validateSource(source);
        Path dex =
                GuestJava.dex(new GuestJava.Unit(source, api), platform, work)
                        .resolve("classes.dex");
        byte[] bytes = Files.readAllBytes(dex);
        PassionGeneratedHelperContract.verify(PassionGeneratedHelperContract.SOUND, bytes);

        return dex;
    }

    static Method route(Method m, boolean inverse) throws Exception {
        if (!inverse) {
            validate(m);
        } else {
            need(target(m));
        }

        boolean update = m.getName().equals("updateState"),
                destroy = m.getName().equals("onDestroyInternal");
        var b = new MutableMethodImplementation(m.getImplementation());

        if (inverse) {
            removeFallback(b, update, destroy);
        } else if (update) {
            addUpdateFallback(b);
        } else {
            addControllerFallback(b, destroy);
        }

        var debug = preservedDebugItems(m, b);
        var impl =
                new ImmutableMethodImplementation(
                        b.getRegisterCount(), b.getInstructions(), b.getTryBlocks(), debug);
        Method result =
                new ImmutableMethod(
                        m.getDefiningClass(),
                        m.getName(),
                        m.getParameters(),
                        m.getReturnType(),
                        m.getAccessFlags(),
                        m.getAnnotations(),
                        m.getHiddenApiRestrictions(),
                        impl);

        if (inverse) {
            validate(result);
        }

        return result;
    }

    private static void removeFallback(
            MutableMethodImplementation b, boolean update, boolean destroy) {
        int offset = update ? 0 : 1, count = update ? 4 : destroy ? 6 : 2;
        var ins = b.getInstructions();

        need(ins.size() > offset + count);
        var branch = ins.get(offset + (update ? 1 : 0));
        need(
                branch.getOpcode() == Opcode.IF_NEZ
                        && ((OneRegisterInstruction) branch).getRegisterA() == 0
                        && ((OffsetInstruction) branch).getCodeOffset()
                                == (update ? 6 : destroy ? 12 : 3));

        if (update) {
            requireUpdateFallback(ins);
        }

        if (destroy) {
            requireDestroyFallback(ins);
        }

        need(ins.get(offset + count - 1).getOpcode() == Opcode.RETURN_VOID);

        for (int i = 0; i < count; i++) {
            b.removeInstruction(offset);
        }
    }

    private static void requireUpdateFallback(List<? extends Instruction> ins) {
        var field = ins.get(0);

        need(
                field.getOpcode() == Opcode.IGET_OBJECT
                        && ((TwoRegisterInstruction) field).getRegisterA() == 0
                        && ((TwoRegisterInstruction) field).getRegisterB() == 2
                        && ((ReferenceInstruction) field)
                                .getReference()
                                .toString()
                                .equals(OWNER + "->mCar:Landroid/car/Car;"));
        var call = ins.get(2);
        need(
                call.getOpcode() == Opcode.INVOKE_STATIC
                        && ((FiveRegisterInstruction) call).getRegisterCount() == 1
                        && ((FiveRegisterInstruction) call).getRegisterC() == 3
                        && ((ReferenceInstruction) call)
                                .getReference()
                                .toString()
                                .equals(
                                        "Lcom/voyemu/settings/PassionSoundStreams;->update(Landroidx/preference/PreferenceGroup;)V"));
    }

    private static void requireDestroyFallback(List<? extends Instruction> ins) {
        var get = ins.get(2);

        need(
                get.getOpcode() == Opcode.INVOKE_VIRTUAL
                        && ((FiveRegisterInstruction) get).getRegisterCount() == 1
                        && ((FiveRegisterInstruction) get).getRegisterC() == 1
                        && ((ReferenceInstruction) get)
                                .getReference()
                                .toString()
                                .equals(
                                        "Lcom/android/car/settings/common/PreferenceController;->getPreference()Landroidx/preference/Preference;"));
        need(
                ins.get(3).getOpcode() == Opcode.MOVE_RESULT_OBJECT
                        && ((OneRegisterInstruction) ins.get(3)).getRegisterA() == 0);
        need(
                ins.get(4).getOpcode() == Opcode.CHECK_CAST
                        && ((OneRegisterInstruction) ins.get(4)).getRegisterA() == 0
                        && ((ReferenceInstruction) ins.get(4))
                                .getReference()
                                .toString()
                                .equals("Landroidx/preference/PreferenceGroup;"));
        var dispose = ins.get(5);
        need(
                dispose.getOpcode() == Opcode.INVOKE_STATIC
                        && ((FiveRegisterInstruction) dispose).getRegisterCount() == 1
                        && ((FiveRegisterInstruction) dispose).getRegisterC() == 0
                        && ((ReferenceInstruction) dispose)
                                .getReference()
                                .toString()
                                .equals(
                                        "Lcom/voyemu/settings/PassionSoundStreams;->dispose(Landroidx/preference/PreferenceGroup;)V"));
    }

    private static void addUpdateFallback(MutableMethodImplementation b) {
        var original = b.newLabelForIndex(0);

        b.addInstruction(
                0,
                new BuilderInstruction22c(
                        Opcode.IGET_OBJECT,
                        0,
                        2,
                        new ImmutableFieldReference(OWNER, "mCar", "Landroid/car/Car;")));
        b.addInstruction(1, new BuilderInstruction21t(Opcode.IF_NEZ, 0, original));
        b.addInstruction(
                2,
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        1,
                        3,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                "Lcom/voyemu/settings/PassionSoundStreams;",
                                "update",
                                List.of("Landroidx/preference/PreferenceGroup;"),
                                "V")));
        b.addInstruction(3, new BuilderInstruction10x(Opcode.RETURN_VOID));
    }

    private static void addControllerFallback(MutableMethodImplementation b, boolean destroy) {
        var original = b.newLabelForIndex(1);

        b.addInstruction(1, new BuilderInstruction21t(Opcode.IF_NEZ, 0, original));

        if (destroy) {
            b.addInstruction(
                    2,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_VIRTUAL,
                            1,
                            1,
                            0,
                            0,
                            0,
                            0,
                            new ImmutableMethodReference(
                                    "Lcom/android/car/settings/common/PreferenceController;",
                                    "getPreference",
                                    List.of(),
                                    "Landroidx/preference/Preference;")));
            b.addInstruction(3, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0));
            b.addInstruction(
                    4,
                    new BuilderInstruction21c(
                            Opcode.CHECK_CAST,
                            0,
                            new ImmutableTypeReference("Landroidx/preference/PreferenceGroup;")));
            b.addInstruction(
                    5,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_STATIC,
                            1,
                            0,
                            0,
                            0,
                            0,
                            0,
                            new ImmutableMethodReference(
                                    "Lcom/voyemu/settings/PassionSoundStreams;",
                                    "dispose",
                                    List.of("Landroidx/preference/PreferenceGroup;"),
                                    "V")));
            b.addInstruction(6, new BuilderInstruction10x(Opcode.RETURN_VOID));
        } else {
            b.addInstruction(2, new BuilderInstruction10x(Opcode.RETURN_VOID));
        }
    }

    private static List<DebugItem> preservedDebugItems(Method m, MutableMethodImplementation b) {
        var debug = new ArrayList<DebugItem>();
        var old = m.getImplementation().getDebugItems().iterator();

        for (var x : b.getDebugItems()) {
            need(old.hasNext());
            var p = old.next();
            need(p.getDebugItemType() == x.getDebugItemType());

            if (x instanceof EndLocal e && p instanceof EndLocal y) {
                debug.add(
                        new ImmutableEndLocal(
                                x.getCodeAddress(),
                                e.getRegister(),
                                y.getName(),
                                y.getType(),
                                y.getSignature()));
            } else if (x instanceof RestartLocal e && p instanceof RestartLocal y) {
                debug.add(
                        new ImmutableRestartLocal(
                                x.getCodeAddress(),
                                e.getRegister(),
                                y.getName(),
                                y.getType(),
                                y.getSignature()));
            } else {
                debug.add(x);
            }
        }

        need(!old.hasNext());

        return debug;
    }

    static ImmutableDexFile rewrite(DexFile source, boolean inverse) throws Exception {
        var all = new ArrayList<ClassDef>();
        int count = 0;

        for (var c : source.getClasses()) {
            if (!c.getType().equals(OWNER)) {
                all.add(c);
                continue;
            }

            var methods = new ArrayList<Method>();

            for (var m : c.getVirtualMethods()) {
                if (target(m)) {
                    m = route(m, inverse);
                    count++;
                }

                methods.add(m);
            }

            all.add(
                    new ImmutableClassDef(
                            c.getType(),
                            c.getAccessFlags(),
                            c.getSuperclass(),
                            c.getInterfaces(),
                            c.getSourceFile(),
                            c.getAnnotations(),
                            c.getStaticFields(),
                            c.getInstanceFields(),
                            c.getDirectMethods(),
                            methods));
        }

        need(count == 3);

        return new ImmutableDexFile(Opcodes.forApi(30), all);
    }

    static byte[] apply(String profile, int api, Path apk, byte[] dex) throws Exception {
        validate(profile, api, apk, dex);
        var before = new DexBackedDexFile(Opcodes.forApi(30), dex);
        var expected = rewrite(before, false);
        var out = FrameworkDexMaterializer.serialize(expected);
        var actual = new DexBackedDexFile(Opcodes.forApi(30), out);
        need(
                FrameworkDexMaterializer.semantic(expected.getClasses())
                        .equals(FrameworkDexMaterializer.semantic(actual.getClasses())));
        need(
                FrameworkDexMaterializer.semantic(before.getClasses())
                        .equals(
                                FrameworkDexMaterializer.semantic(
                                        rewrite(actual, true).getClasses())));

        return out;
    }
}
