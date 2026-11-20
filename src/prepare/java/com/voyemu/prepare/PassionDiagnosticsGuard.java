package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableExceptionHandler;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.ImmutableTryBlock;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeMap;

/**
 * Missing optional diagnostic HAL follows the existing null-service path; no MAC or HAL is
 * invented.
 */
final class PassionDiagnosticsGuard {
    static final String NAME = "passion-api30-diagnostics";
    static final String PIN = "5cd091682a3ba6aa054a0b545a2c5a853e3e5bff7129f7319ebe8d583eccea98";
    static final String OWNER = "Lcom/mega/car/service/BluetoothMacReader;";
    static final String MISSING = "Ljava/util/NoSuchElementException;";

    private static void require(boolean value) {
        Failure.require(
                value,
                "PASSION_DIAGNOSTICS_CONTRACT",
                "Exact owner-pinned Passion optional diagnostic boundary required",
                3);
    }

    static Method method(org.jf.dexlib2.iface.DexFile dex) {
        Method result = null;

        for (var type : dex.getClasses()) {
            if (type.getType().equals(OWNER)) {
                for (var candidate : type.getVirtualMethods()) {
                    if (candidate.getName().equals("init")) {
                        require(
                                result == null
                                        && candidate.getParameterTypes().isEmpty()
                                        && candidate.getReturnType().equals("V")
                                        && candidate.getAccessFlags() == 1);
                        result = candidate;
                    }
                }
            }
        }

        require(result != null && result.getImplementation() != null);

        return result;
    }

    static void validate(String profile, int api, Path original, byte[] dex) throws Exception {
        require(api == 30 && FilesSupport.hash(original).equals(PIN));
        require(dex != null);
        var body = method(new DexBackedDexFile(Opcodes.forApi(30), dex)).getImplementation();
        require(body.getRegisterCount() == 7 && body.getTryBlocks().size() == 1);
        var block = body.getTryBlocks().get(0);
        require(
                block.getStartCodeAddress() == 0x12
                        && block.getCodeUnitCount() == 6
                        && block.getExceptionHandlers().size() == 1);
        var handler = block.getExceptionHandlers().get(0);
        require(
                "Landroid/os/RemoteException;".equals(handler.getExceptionType())
                        && handler.getHandlerCodeAddress() == 0x19);
        int address = 0, guards = 0;

        for (var instruction : body.getInstructions()) {
            if (address == 0x12) {
                require(
                        instruction.getOpcode() == Opcode.INVOKE_STATIC
                                && instruction instanceof ReferenceInstruction referenceInstruction
                                && referenceInstruction
                                        .getReference()
                                        .toString()
                                        .equals(
                                                "Lvendor/mega/diagnostics/V1_0/IDiagnosticsService;->getService()Lvendor/mega/diagnostics/V1_0/IDiagnosticsService;"));
                guards++;
            }

            if (address == 0x19) {
                require(instruction.getOpcode() == Opcode.MOVE_EXCEPTION);
                guards++;
            }

            if (address == 0x1a) {
                require(
                        instruction instanceof ReferenceInstruction referenceInstruction
                                && referenceInstruction
                                        .getReference()
                                        .toString()
                                        .equals(
                                                "Landroid/os/RemoteException;->printStackTrace()V"));
                guards++;
            }

            if (address == 0x1d) {
                require(
                        instruction.getOpcode() == Opcode.INVOKE_DIRECT
                                && instruction instanceof ReferenceInstruction referenceInstruction
                                && referenceInstruction
                                        .getReference()
                                        .toString()
                                        .equals(OWNER + "->readMac()V"));
                guards++;
            }

            address += instruction.getCodeUnits();
        }

        require(address == 0x21 && guards == 4);
    }

    private static Method replace(Method method, org.jf.dexlib2.iface.MethodImplementation body) {
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

    static byte[] apply(String profile, int api, Path original, byte[] dex) throws Exception {
        validate(profile, api, original, dex);
        var source = new DexBackedDexFile(Opcodes.forApi(30), dex);
        var selected = method(source);
        var body = selected.getImplementation();
        var handlers = new ArrayList<ImmutableExceptionHandler>();

        for (var handler : body.getTryBlocks().get(0).getExceptionHandlers()) {
            handlers.add(ImmutableExceptionHandler.of(handler));
        }

        // Discard the optional-HAL exception, entering existing readMac/null writeDID fallback.
        // Do not enter the RemoteException-typed printStackTrace handler; all instructions remain
        // intact.
        handlers.add(new ImmutableExceptionHandler(MISSING, 0x1d));
        var changed =
                new ImmutableMethodImplementation(
                        body.getRegisterCount(),
                        body.getInstructions(),
                        List.of(new ImmutableTryBlock(0x12, 6, handlers)),
                        body.getDebugItems());
        var classes = new TreeMap<String, ClassDef>();

        for (var type : source.getClasses()) {
            if (!type.getType().equals(OWNER)) {
                classes.put(type.getType(), type);
                continue;
            }

            var methods = new ArrayList<Method>();

            for (var candidate : type.getVirtualMethods()) {
                methods.add(
                        candidate.getName().equals("init")
                                        && candidate.getParameterTypes().isEmpty()
                                        && candidate.getReturnType().equals("V")
                                ? replace(candidate, changed)
                                : candidate);
            }

            classes.put(
                    type.getType(),
                    new ImmutableClassDef(
                            type.getType(),
                            type.getAccessFlags(),
                            type.getSuperclass(),
                            type.getInterfaces(),
                            type.getSourceFile(),
                            type.getAnnotations(),
                            type.getStaticFields(),
                            type.getInstanceFields(),
                            type.getDirectMethods(),
                            methods));
        }

        byte[] output =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), classes.values()));
        var reread = new DexBackedDexFile(Opcodes.forApi(30), output);
        require(
                method(reread)
                                .getImplementation()
                                .getTryBlocks()
                                .get(0)
                                .getExceptionHandlers()
                                .size()
                        == 2);
        var normalized = new TreeMap<String, ClassDef>();

        for (var type : reread.getClasses()) {
            if (!type.getType().equals(OWNER)) {
                normalized.put(type.getType(), type);
                continue;
            }

            var methods = new ArrayList<Method>();

            for (Method candidate : type.getVirtualMethods()) {
                if (candidate.getName().equals("init")
                        && candidate.getParameterTypes().isEmpty()
                        && candidate.getReturnType().equals("V")) {
                    var current = candidate.getImplementation();
                    candidate =
                            replace(
                                    candidate,
                                    new ImmutableMethodImplementation(
                                            current.getRegisterCount(),
                                            current.getInstructions(),
                                            body.getTryBlocks(),
                                            current.getDebugItems()));
                }

                methods.add(candidate);
            }

            normalized.put(
                    type.getType(),
                    new ImmutableClassDef(
                            type.getType(),
                            type.getAccessFlags(),
                            type.getSuperclass(),
                            type.getInterfaces(),
                            type.getSourceFile(),
                            type.getAnnotations(),
                            type.getStaticFields(),
                            type.getInstanceFields(),
                            type.getDirectMethods(),
                            methods));
        }

        require(source.getClasses().size() == normalized.size());

        for (var type : source.getClasses()) {
            require(
                    normalized.containsKey(type.getType())
                            && FrameworkDexMaterializer.semantic(type)
                                    .equals(
                                            FrameworkDexMaterializer.semantic(
                                                    normalized.get(type.getType()))));
        }

        return output;
    }
}
