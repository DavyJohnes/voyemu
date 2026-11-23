package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/** Keeps OEM DVR capability and CameraX selection except at the emulated camera boundary. */
final class DvrCameraVirtualGuard {
    static final String NAME = "api30-dvr-camera-virtual",
            HELPER_ROOT = NAME + "/src",
            HELPER_SOURCE = "com/voyemu/camera/DvrBackCamera.java",
            HELPER = "Lcom/voyemu/camera/DvrBackCamera;";
    private static final String SUPPORT = "Lcom/qinggan/dvr/common/SupportUtils;",
            FILTER = "Lcom/qinggan/dvr/fragments/preview/DVRCameraFilter;",
            READ = LauncherDvrGuard.Recomputed.READ,
            DVR = LauncherDvrGuard.Recomputed.DVR,
            LIST = "Ljava/util/List;";

    private DvrCameraVirtualGuard() {}

    private static void need(boolean valid) {
        Failure.require(valid, "DVR_CAMERA_VIRTUAL", "Exact OEM DVR camera boundary required", 3);
    }

    static byte[] patch(
            Map<String, Object> constants, int api, String abi, Path apk, byte[] raw, boolean apply)
            throws Exception {
        need(
                api == 30
                        && "arm64-v8a".equals(abi)
                        && StepConstants.admits(constants, FilesSupport.hash(apk))
                        && raw != null);
        var classes = CameraVirtualGuard.classes(raw);
        var support = LauncherDvrGuard.Recomputed.method(classes, SUPPORT, "isSupportDvr");
        var filter = LauncherDvrGuard.Recomputed.method(classes, FILTER, "filter");
        need(
                support.getParameterTypes().isEmpty()
                        && support.getReturnType().equals("Z")
                        && filter.getParameterTypes().equals(List.of(LIST))
                        && filter.getReturnType().equals(LIST));
        var patchedSupport = adaptSupport(support);
        var patchedFilter = adaptFilter(filter);

        if (!apply) {
            return raw;
        }

        Map<String, ClassDef> changed = new TreeMap<>(classes);
        changed.put(SUPPORT, replace(classes.get(SUPPORT), patchedSupport));
        changed.put(FILTER, replace(classes.get(FILTER), patchedFilter));
        byte[] result =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), changed.values()));
        var reread = CameraVirtualGuard.classes(result);
        need(changed.keySet().equals(reread.keySet()));

        for (var entry : changed.entrySet()) {
            need(
                    FrameworkDexMaterializer.semantic(entry.getValue())
                            .equals(FrameworkDexMaterializer.semantic(reread.get(entry.getKey()))));
        }

        return result;
    }

    private static Method adaptSupport(Method method) {
        var code = new MutableMethodImplementation(method.getImplementation());
        var instructions = code.getInstructions();
        int lastRead = -1;
        int reads = 0;
        int adapters = 0;

        for (int i = 0; i < instructions.size(); ++i) {
            var instruction = instructions.get(i);

            if (instruction.getOpcode() == Opcode.INVOKE_STATIC
                    && ((ReferenceInstruction) instruction)
                            .getReference()
                            .toString()
                            .equals(READ)) {
                need(
                        i > 0
                                && i + 1 < instructions.size()
                                && instructions.get(i - 1).getOpcode() == Opcode.SGET_OBJECT
                                && ((ReferenceInstruction) instructions.get(i - 1))
                                        .getReference()
                                        .toString()
                                        .equals(DVR)
                                && instructions.get(i + 1).getOpcode() == Opcode.MOVE_RESULT);
                lastRead = i;
                ++reads;
            }

            if (instruction instanceof ReferenceInstruction reference
                    && reference.getReference().toString().startsWith(HELPER + "->")) {
                ++adapters;
            }
        }

        need(reads == 2 && adapters == 0);
        int at = lastRead + 2;
        int reg = ((OneRegisterInstruction) instructions.get(lastRead + 1)).getRegisterA();
        code.addInstruction(at, invoke("capability", List.of("I"), "I", reg, 0));
        code.addInstruction(at + 1, new BuilderInstruction11x(Opcode.MOVE_RESULT, reg));

        return CameraVirtualGuard.copy(method, code);
    }

    private static Method adaptFilter(Method method) {
        var code = new MutableMethodImplementation(method.getImplementation());
        var instructions = code.getInstructions();
        int result = -1;

        for (int i = 0; i < instructions.size(); ++i) {
            var instruction = instructions.get(i);
            need(
                    !(instruction instanceof ReferenceInstruction reference
                            && reference.getReference().toString().startsWith(HELPER + "->")));

            if (instruction.getOpcode() == Opcode.RETURN_OBJECT) {
                need(result == -1);
                result = i;
            }
        }

        need(result >= 0);
        int output = ((OneRegisterInstruction) instructions.get(result)).getRegisterA();
        int input = method.getImplementation().getRegisterCount() - 1;
        need(output != input);
        code.replaceInstruction(result, invoke("select", List.of(LIST, LIST), LIST, input, output));
        code.addInstruction(
                result + 1, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, output));
        code.addInstruction(result + 2, new BuilderInstruction11x(Opcode.RETURN_OBJECT, output));

        return CameraVirtualGuard.copy(method, code);
    }

    private static BuilderInstruction35c invoke(
            String name, List<String> types, String result, int first, int second) {
        need(first >= 0 && first <= 15 && second >= 0 && second <= 15);

        return new BuilderInstruction35c(
                Opcode.INVOKE_STATIC,
                types.size(),
                first,
                second,
                0,
                0,
                0,
                new ImmutableMethodReference(HELPER, name, types, result));
    }

    private static ClassDef replace(ClassDef owner, Method replacement) {
        var direct = new ArrayList<Method>();
        var virtual = new ArrayList<Method>();

        for (var method : owner.getDirectMethods()) {
            direct.add(method.getName().equals(replacement.getName()) ? replacement : method);
        }

        for (var method : owner.getVirtualMethods()) {
            virtual.add(method.getName().equals(replacement.getName()) ? replacement : method);
        }

        return new ImmutableClassDef(
                owner.getType(),
                owner.getAccessFlags(),
                owner.getSuperclass(),
                owner.getInterfaces(),
                owner.getSourceFile(),
                owner.getAnnotations(),
                owner.getStaticFields(),
                owner.getInstanceFields(),
                direct,
                virtual);
    }
}
