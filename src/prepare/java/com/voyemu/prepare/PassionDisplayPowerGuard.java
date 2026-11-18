package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction3rc;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.RegisterRangeInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/** Exact pinned SDK power hooks; helper placement is a separate ordinary recipe dependency. */
public final class PassionDisplayPowerGuard {
    static final String NAME = "passion-api30-display-power";
    static final String PROFILE = "passion-11-russian-20260403-2.1.4.80",
            PIN = "c6992cdc2a3e23d5aeb6ab77e7a7f6889cfcf5c262c6ed86825e6fcbf601681d",
            DMS = "Lcom/android/server/display/DisplayManagerService;",
            BINDER = "Lcom/android/server/display/DisplayManagerService$BinderService;";

    static void check(boolean b) {
        if (!b) {
            throw new IllegalArgumentException(
                    "Exact canonical API30 DMS two-hook contract required");
        }
    }

    static boolean target(Method m) {
        return (m.getDefiningClass().equals(BINDER) && m.getName().equals("onShellCommand"))
                || (m.getDefiningClass().equals(DMS)
                        && m.getName().equals("updateDisplayStateLocked"));
    }

    static void validate(String profile, int api, Path jar, byte[] dex) throws Exception {
        check(
                PROFILE.equals(profile)
                        && api == 30
                        && FilesSupport.hash(jar).equals(PIN)
                        && dex != null);
        int count = 0;

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            for (var m : c.getMethods()) {
                if (target(m)) {
                    check(
                            m.getImplementation() != null
                                    && m.getImplementation().getTryBlocks().isEmpty()
                                    && m.getImplementation().getRegisterCount()
                                            == (m.getName().equals("onShellCommand") ? 15 : 5));
                    count++;
                }
            }
        }

        check(count == 2);
    }

    static Method route(Method method, boolean inverse) {
        var body = new MutableMethodImplementation(method.getImplementation());
        boolean shell = method.getName().equals("onShellCommand");
        int address = 0, index = -1;

        for (int i = 0; i < body.getInstructions().size(); i++) {
            if (address == (shell ? 14 : 12)) {
                index = i;
                break;
            }

            address += body.getInstructions().get(i).getCodeUnits();
        }

        check(index >= 0);

        if (shell) {
            var old = body.getInstructions().get(index);
            check(
                    old.getOpcode()
                                    == (inverse
                                            ? Opcode.INVOKE_STATIC_RANGE
                                            : Opcode.INVOKE_VIRTUAL_RANGE)
                            && old instanceof RegisterRangeInstruction registerRangeInstruction
                            && registerRangeInstruction.getStartRegister() == 0
                            && registerRangeInstruction.getRegisterCount() == 8);
            String
                    original =
                            "Lcom/android/server/display/DisplayManagerShellCommand;->exec(Landroid/os/Binder;Ljava/io/FileDescriptor;Ljava/io/FileDescriptor;Ljava/io/FileDescriptor;[Ljava/lang/String;Landroid/os/ShellCallback;Landroid/os/ResultReceiver;)I",
                    helper =
                            "Lcom/voyemu/display/DmsPowerCommand;->exec(Ljava/lang/Object;Ljava/lang/Object;Ljava/io/FileDescriptor;Ljava/io/FileDescriptor;Ljava/io/FileDescriptor;[Ljava/lang/String;Ljava/lang/Object;Ljava/lang/Object;)I";
            check(
                    ((ReferenceInstruction) old)
                            .getReference()
                            .toString()
                            .equals(inverse ? helper : original));
            var params =
                    inverse
                            ? List.of(
                                    "Landroid/os/Binder;",
                                    "Ljava/io/FileDescriptor;",
                                    "Ljava/io/FileDescriptor;",
                                    "Ljava/io/FileDescriptor;",
                                    "[Ljava/lang/String;",
                                    "Landroid/os/ShellCallback;",
                                    "Landroid/os/ResultReceiver;")
                            : List.of(
                                    "Ljava/lang/Object;",
                                    "Ljava/lang/Object;",
                                    "Ljava/io/FileDescriptor;",
                                    "Ljava/io/FileDescriptor;",
                                    "Ljava/io/FileDescriptor;",
                                    "[Ljava/lang/String;",
                                    "Ljava/lang/Object;",
                                    "Ljava/lang/Object;");
            body.replaceInstruction(
                    index,
                    new BuilderInstruction3rc(
                            inverse ? Opcode.INVOKE_VIRTUAL_RANGE : Opcode.INVOKE_STATIC_RANGE,
                            0,
                            8,
                            new ImmutableMethodReference(
                                    inverse
                                            ? "Lcom/android/server/display/DisplayManagerShellCommand;"
                                            : "Lcom/voyemu/display/DmsPowerCommand;",
                                    "exec",
                                    params,
                                    "I")));
        } else if (inverse) {
            var call = body.getInstructions().get(index);
            check(
                    call.getOpcode() == Opcode.INVOKE_STATIC
                            && ((ReferenceInstruction) call)
                                    .getReference()
                                    .toString()
                                    .equals(
                                            "Lcom/voyemu/display/DmsDisplayPowerBackend;->effectiveState(Ljava/lang/Object;I)I")
                            && ((FiveRegisterInstruction) call).getRegisterCount() == 2
                            && ((FiveRegisterInstruction) call).getRegisterC() == 4
                            && ((FiveRegisterInstruction) call).getRegisterD() == 1);
            check(
                    body.getInstructions().get(index + 1).getOpcode() == Opcode.MOVE_RESULT
                            && ((OneRegisterInstruction) body.getInstructions().get(index + 1))
                                            .getRegisterA()
                                    == 1);
            body.removeInstruction(index + 1);
            body.removeInstruction(index);
        } else {
            var old = body.getInstructions().get(index);
            check(
                    old.getOpcode() == Opcode.IGET
                            && ((ReferenceInstruction) old)
                                    .getReference()
                                    .toString()
                                    .equals(DMS + "->mGlobalDisplayBrightness:F"));
            body.addInstruction(
                    index,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_STATIC,
                            2,
                            4,
                            1,
                            0,
                            0,
                            0,
                            new ImmutableMethodReference(
                                    "Lcom/voyemu/display/DmsDisplayPowerBackend;",
                                    "effectiveState",
                                    List.of("Ljava/lang/Object;", "I"),
                                    "I")));
            body.addInstruction(index + 1, new BuilderInstruction11x(Opcode.MOVE_RESULT, 1));
        }

        return new ImmutableMethod(
                method.getDefiningClass(),
                method.getName(),
                method.getParameters(),
                method.getReturnType(),
                method.getAccessFlags(),
                method.getAnnotations(),
                method.getHiddenApiRestrictions(),
                body);
    }

    static ImmutableDexFile rewrite(DexFile input, boolean inverse) {
        List<ClassDef> classes = new ArrayList<>();
        int count = 0;

        for (var c : input.getClasses()) {
            List<Method> direct = new ArrayList<>(), virtual = new ArrayList<>();

            for (var m : c.getDirectMethods()) {
                if (target(m)) {
                    m = route(m, inverse);
                    count++;
                }

                direct.add(m);
            }

            for (var m : c.getVirtualMethods()) {
                if (target(m)) {
                    m = route(m, inverse);
                    count++;
                }

                virtual.add(m);
            }

            classes.add(
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

        check(count == 2);

        return new ImmutableDexFile(Opcodes.forApi(30), classes);
    }

    static byte[] apply(String profile, int api, Path jar, byte[] dex) throws Exception {
        validate(profile, api, jar, dex);
        var source = new DexBackedDexFile(Opcodes.forApi(30), dex);
        byte[] out = FrameworkDexMaterializer.serialize(rewrite(source, false));
        var restored = rewrite(new DexBackedDexFile(Opcodes.forApi(30), out), true);
        Map<String, ClassDef> names = new TreeMap<>();

        for (var c : restored.getClasses()) {
            names.put(c.getType(), c);
        }

        check(names.size() == source.getClasses().size());

        for (var c : source.getClasses()) {
            check(
                    FrameworkDexMaterializer.semantic(c)
                            .equals(FrameworkDexMaterializer.semantic(names.get(c.getType()))));
        }

        return out;
    }
}
