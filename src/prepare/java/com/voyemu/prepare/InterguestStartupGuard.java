package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction3rc;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.RegisterRangeInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;

/** Inserts the fixed guest Ethernet setup on the successful framework readiness edge. */
final class InterguestStartupGuard {
    static final String NAME = "api30-interguest-startup";
    private static final String OWNER = "Lcom/android/server/SystemServer;";
    private static final String CALLER =
            OWNER
                    + "->lambda$startOtherServices$6$SystemServer("
                    + "Lcom/android/server/utils/TimingsTraceAndSlog;Landroid/content/Context;"
                    + "Lcom/android/server/wm/WindowManagerService;ZLcom/android/server/ConnectivityService;"
                    + "Lcom/android/server/NetworkManagementService;Lcom/android/server/net/NetworkPolicyManagerService;"
                    + "Lcom/android/server/IpSecService;Lcom/android/server/net/NetworkStatsService;"
                    + "Lcom/android/server/CountryDetectorService;Lcom/android/server/NetworkTimeUpdateService;"
                    + "Lcom/android/server/input/InputManagerService;Lcom/android/server/TelephonyRegistry;"
                    + "Lcom/android/server/media/MediaRouterService;Lcom/android/server/MmsServiceBroker;)V";
    private static final String SERVICE = "Lcom/android/server/NetworkManagementService;";
    private static final String CALLBACK = "Lcom/android/server/VoyemuInterguestStartup;";
    private static final String READY = SERVICE + "->systemReady()V";
    private static final ImmutableMethodReference SCHEDULE =
            new ImmutableMethodReference(CALLBACK, "schedule", List.of(SERVICE), "V");

    private InterguestStartupGuard() {}

    private static void require(boolean condition) {
        Failure.require(
                condition,
                "ANDROID_INTERGUEST_STARTUP",
                "Exact pinned API30 Ethernet setup and readiness edge required",
                3);
    }

    static void validate(String profile, int api, Path input, Path source, byte[] primary)
            throws Exception {
        require(
                Api30NativeFiles.QINGGAN11_731.equals(profile)
                        && api == 30
                        && FilesSupport.hash(input).equals(HiboardTopGestureGuard.PIN));
        require(
                FilesSupport.tree(source)
                        .keySet()
                        .equals(
                                Set.of(
                                        "com/android/server/VoyemuInterguestStartup.java",
                                        "compile-only/com/android/server/NetworkManagementService.java")));
        var type = HiboardTopGestureGuard.classes(primary).get(OWNER);
        require(type != null);
        int matched = 0;

        for (Method method : type.getDirectMethods()) {
            if (method.toString().equals(CALLER)) {
                verifyReverse(method, rewrite(method, true));
                ++matched;
            }
        }

        require(matched == 1);
    }

    static byte[] rewritePrimary(byte[] primary) throws Exception {
        var classes = HiboardTopGestureGuard.classes(primary);
        var systemServer = classes.get(OWNER);

        require(systemServer != null);
        var direct = new ArrayList<Method>();
        int changed = 0;

        for (Method method : systemServer.getDirectMethods()) {
            if (method.toString().equals(CALLER)) {
                Method patched = rewrite(method, true);
                verifyReverse(method, patched);
                direct.add(patched);
                ++changed;
            } else {
                direct.add(method);
            }
        }

        require(changed == 1);
        classes.put(
                OWNER,
                new ImmutableClassDef(
                        systemServer.getType(),
                        systemServer.getAccessFlags(),
                        systemServer.getSuperclass(),
                        systemServer.getInterfaces(),
                        systemServer.getSourceFile(),
                        systemServer.getAnnotations(),
                        systemServer.getStaticFields(),
                        systemServer.getInstanceFields(),
                        direct,
                        systemServer.getVirtualMethods()));
        byte[] result =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), classes.values()));
        var reread = HiboardTopGestureGuard.classes(result);
        require(reread.keySet().equals(classes.keySet()));

        for (ClassDef reloaded : reread.values()) {
            require(
                    FrameworkDexMaterializer.semantic(reloaded)
                            .equals(
                                    FrameworkDexMaterializer.semantic(
                                            classes.get(reloaded.getType()))));
        }

        return result;
    }

    private static void verifyReverse(Method original, Method patched) throws Exception {
        require(
                FrameworkDexMaterializer.semantic(original.getImplementation())
                        .equals(
                                FrameworkDexMaterializer.semantic(
                                        rewrite(patched, false).getImplementation())));
    }

    private static Method rewrite(Method method, boolean forward) {
        require(method.getImplementation() != null && method.getReturnType().equals("V"));
        var code = new MutableMethodImplementation(method.getImplementation());
        var instructions = code.getInstructions();
        int found = 0;

        for (int index = 1; index + 2 < instructions.size(); ++index) {
            var lookup = instructions.get(index);

            if (!(lookup instanceof ReferenceInstruction reference)
                    || !reference.getReference().toString().equals(READY)) {
                continue;
            }

            var nullable = instructions.get(index - 1);
            var success = instructions.get(index + 1);
            require(
                    lookup.getOpcode() == Opcode.INVOKE_VIRTUAL_RANGE
                            && lookup instanceof RegisterRangeInstruction registers
                            && registers.getStartRegister() == 19
                            && registers.getRegisterCount() == 1
                            && lookup.getLocation().getCodeAddress() == 0x96
                            && nullable.getOpcode() == Opcode.IF_EQZ
                            && nullable.getLocation().getCodeAddress() == 0x94
                            && success.getLocation().getCodeAddress() == 0x99);
            require(
                    code.getTryBlocks().stream()
                            .anyMatch(block ->
                                    block.getStartCodeAddress() == 0x96
                                            && block.getCodeUnitCount() == 3));

            if (forward) {
                require(
                        success.getOpcode() == Opcode.GOTO
                                && instructions.get(index + 2).getOpcode()
                                        == Opcode.MOVE_EXCEPTION);
                // Keep the original try-end label at the first instruction after systemReady.
                code.replaceInstruction(
                        index + 1,
                        new BuilderInstruction3rc(Opcode.INVOKE_STATIC_RANGE, 19, 1, SCHEDULE));
                code.addInstruction(index + 2, success);
            } else {
                require(
                        success.getOpcode() == Opcode.INVOKE_STATIC_RANGE
                                && success instanceof ReferenceInstruction hook
                                && hook.getReference().toString().equals(SCHEDULE.toString()));
                var originalGoto = instructions.get(index + 2);
                require(originalGoto.getOpcode() == Opcode.GOTO);
                code.removeInstruction(index + 2);
                code.replaceInstruction(index + 1, originalGoto);
            }

            require(
                    code.getTryBlocks().stream()
                            .anyMatch(block ->
                                    block.getStartCodeAddress() == 0x96
                                            && block.getCodeUnitCount() == 3));
            ++found;
        }

        require(found == 1);

        return new ImmutableMethod(
                method.getDefiningClass(),
                method.getName(),
                method.getParameters(),
                method.getReturnType(),
                method.getAccessFlags(),
                method.getAnnotations(),
                method.getHiddenApiRestrictions(),
                code);
    }

    static byte[] addHelper(byte[] secondary, byte[] helper) throws Exception {
        var classes = HiboardTopGestureGuard.classes(secondary);
        var additions = HiboardTopGestureGuard.classes(helper);

        require(
                additions
                        .keySet()
                        .equals(
                                Set.of(
                                        CALLBACK,
                                        "Lcom/android/server/VoyemuInterguestStartup$Attempt;")));

        for (var addition : additions.entrySet()) {
            require(classes.putIfAbsent(addition.getKey(), addition.getValue()) == null);
        }

        return FrameworkDexMaterializer.serialize(
                new ImmutableDexFile(Opcodes.forApi(30), classes.values()));
    }
}
