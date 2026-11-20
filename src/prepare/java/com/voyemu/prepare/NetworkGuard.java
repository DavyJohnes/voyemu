package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.debug.DebugItem;
import org.jf.dexlib2.iface.debug.EndLocal;
import org.jf.dexlib2.iface.debug.EpilogueBegin;
import org.jf.dexlib2.iface.debug.LineNumber;
import org.jf.dexlib2.iface.debug.PrologueEnd;
import org.jf.dexlib2.iface.debug.RestartLocal;
import org.jf.dexlib2.iface.debug.SetSourceFile;
import org.jf.dexlib2.iface.debug.StartLocal;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.debug.ImmutableDebugItem;
import org.jf.dexlib2.immutable.debug.ImmutableEndLocal;
import org.jf.dexlib2.immutable.debug.ImmutableEpilogueBegin;
import org.jf.dexlib2.immutable.debug.ImmutableLineNumber;
import org.jf.dexlib2.immutable.debug.ImmutablePrologueEnd;
import org.jf.dexlib2.immutable.debug.ImmutableRestartLocal;
import org.jf.dexlib2.immutable.debug.ImmutableSetSourceFile;
import org.jf.dexlib2.immutable.debug.ImmutableStartLocal;
import org.jf.dexlib2.immutable.reference.ImmutableFieldReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

// Absent Ethernet falls through to WiFi; absent WiFi returns the existing false result.
final class NetworkGuard {
    static final String NAME = "api30-network";
    static final String API28_RECIPE = "systemservice-network.json";
    static final String OWNER = "Lcom/qinggan/systemservice/displaymanager/DisplayManagerImpl;";
    static final String RECEIVER =
            "Lcom/qinggan/systemservice/displaymanager/DisplayManagerImpl$13;";
    static final String QUERY =
            "Landroid/net/ConnectivityManager;->getNetworkInfo(I)Landroid/net/NetworkInfo;";

    private static void require(boolean value) {
        Failure.require(
                value,
                "NETWORK_CONTRACT",
                "Exact owner-pinned nullable network boundary required",
                3);
    }

    private static boolean target(Method method) {
        return method.getName().equals("isNetWorkConnected")
                && method.getParameterTypes().isEmpty()
                && method.getReturnType().equals("Z");
    }

    private static ClassDef type(byte[] dex, int api, String name) {
        ClassDef selected = null;

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(api), dex).getClasses()) {
            if (type.getType().equals(name)) {
                require(selected == null);
                selected = type;
            }
        }

        require(selected != null);

        return selected;
    }

    private static Method method(ClassDef type) {
        Method selected = null;

        for (Method method : type.getDirectMethods()) {
            if (target(method)) {
                require(selected == null);
                selected = method;
            }
        }

        require(
                selected != null
                        && selected.getAccessFlags() == 2
                        && selected.getImplementation() != null);
        require(
                selected.getImplementation().getRegisterCount() == 5
                        && selected.getImplementation().getTryBlocks().isEmpty());

        return selected;
    }

    private static Method receiver(ClassDef type) {
        Method selected = null;

        for (Method candidate : type.getVirtualMethods()) {
            if (candidate.getName().equals("onReceive")) {
                require(
                        selected == null
                                && candidate
                                        .getParameterTypes()
                                        .equals(
                                                List.of(
                                                        "Landroid/content/Context;",
                                                        "Landroid/content/Intent;"))
                                && candidate.getReturnType().equals("V"));
                selected = candidate;
            }
        }

        require(
                selected != null
                        && selected.getAccessFlags() == 1
                        && selected.getImplementation() != null);
        require(
                selected.getImplementation().getRegisterCount() == 14
                        && selected.getImplementation().getTryBlocks().isEmpty());

        return selected;
    }

    private static void validateReceiver(Method receiver) {
        var instructions = new ArrayList<Instruction>();

        receiver.getImplementation().getInstructions().forEach(instructions::add);
        int address = 0, queries = 0;

        for (int index = 0; index < instructions.size(); index++) {
            Instruction instruction = instructions.get(index);

            if (instruction instanceof ReferenceInstruction reference
                    && reference.getReference().toString().equals(QUERY)) {
                require(
                        queries < 2
                                && address == (queries == 0 ? 0xcf : 0xdf)
                                && index + 3 < instructions.size());
                Instruction result = instructions.get(index + 1),
                        state = instructions.get(index + 2),
                        stateResult = instructions.get(index + 3);
                require(
                        result.getOpcode() == Opcode.MOVE_RESULT_OBJECT
                                && result instanceof OneRegisterInstruction oneRegisterInstruction
                                && oneRegisterInstruction.getRegisterA() == 4);
                require(
                        state.getOpcode() == Opcode.INVOKE_VIRTUAL
                                && state instanceof ReferenceInstruction referenceInstruction
                                && referenceInstruction
                                        .getReference()
                                        .toString()
                                        .equals(
                                                "Landroid/net/NetworkInfo;->getState()Landroid/net/NetworkInfo$State;"));
                require(
                        stateResult.getOpcode() == Opcode.MOVE_RESULT_OBJECT
                                && stateResult
                                        instanceof OneRegisterInstruction oneRegisterInstruction
                                && oneRegisterInstruction.getRegisterA() == (queries == 0 ? 1 : 0));
                queries++;
            }

            address += instruction.getCodeUnits();
        }

        require(queries == 2);
    }

    static void validate(Map<String, Object> constants, int api, Path original, byte[] dex)
            throws Exception {
        require(
                (api == 28 || api == 30)
                        && StepConstants.admits(constants, FilesSupport.hash(original)));
        require(dex != null);
        validateDefinitions(type(dex, api, OWNER), type(dex, api, RECEIVER));
    }

    private static void validateDefinitions(ClassDef owner, ClassDef receiver) {
        Method selected = method(owner);
        List<Instruction> instructions = new ArrayList<>();

        selected.getImplementation().getInstructions().forEach(instructions::add);
        int address = 0, queries = 0;

        for (int index = 0; index < instructions.size(); index++) {
            Instruction instruction = instructions.get(index);

            if (instruction instanceof ReferenceInstruction reference
                    && reference.getReference().toString().equals(QUERY)) {
                require(
                        queries < 2
                                && address == (queries == 0 ? 0xa : 0x18)
                                && index + 2 < instructions.size());
                Instruction result = instructions.get(index + 1),
                        next = instructions.get(index + 2);
                require(
                        result.getOpcode() == Opcode.MOVE_RESULT_OBJECT
                                && result instanceof OneRegisterInstruction oneRegisterInstruction
                                && oneRegisterInstruction.getRegisterA() == 2);
                require(
                        next.getOpcode() == Opcode.INVOKE_VIRTUAL
                                && next instanceof ReferenceInstruction referenceInstruction
                                && referenceInstruction
                                        .getReference()
                                        .toString()
                                        .equals(
                                                "Landroid/net/NetworkInfo;->getState()Landroid/net/NetworkInfo$State;"));
                queries++;
            }

            address += instruction.getCodeUnits();
        }

        require(queries == 2);
        validateReceiver(receiver(receiver));
    }

    private static Method guarded(Method method) {
        var body = new MutableMethodImplementation(method.getImplementation());
        // Labels refer to existing instructions; dexlib relocates all branches and debug positions.
        var wifi = body.newLabelForAddress(0x16);
        var disconnected = body.newLabelForAddress(0x25);
        int address = 0;
        var indexes = new ArrayList<Integer>();

        for (int index = 0; index < body.getInstructions().size(); index++) {
            if (address == 0xe || address == 0x1c) {
                indexes.add(index);
            }

            address += body.getInstructions().get(index).getCodeUnits();
        }

        require(indexes.size() == 2);
        body.addInstruction(
                indexes.get(1), new BuilderInstruction21t(Opcode.IF_EQZ, 2, disconnected));
        body.addInstruction(indexes.get(0), new BuilderInstruction21t(Opcode.IF_EQZ, 2, wifi));

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

    private static ImmutableMethodImplementation receiverBody(
            Method method, MutableMethodImplementation body) {
        // Builder EndLocal/RestartLocal drop inferred local metadata; retain the pinned original
        // fields at relocated addresses.
        body.getInstructions();
        var original = new ArrayList<DebugItem>();
        method.getImplementation().getDebugItems().forEach(original::add);
        var moved = new ArrayList<DebugItem>();
        body.getDebugItems().forEach(moved::add);
        require(original.size() == moved.size());
        var debug = new ArrayList<ImmutableDebugItem>();

        for (int index = 0; index < original.size(); index++) {
            DebugItem item = original.get(index);
            int at = moved.get(index).getCodeAddress();
            require(item.getDebugItemType() == moved.get(index).getDebugItemType());

            switch (item) {
                case StartLocal local ->
                        debug.add(
                                new ImmutableStartLocal(
                                        at,
                                        local.getRegister(),
                                        local.getName(),
                                        local.getType(),
                                        local.getSignature()));
                case EndLocal local ->
                        debug.add(
                                new ImmutableEndLocal(
                                        at,
                                        local.getRegister(),
                                        local.getName(),
                                        local.getType(),
                                        local.getSignature()));
                case RestartLocal local ->
                        debug.add(
                                new ImmutableRestartLocal(
                                        at,
                                        local.getRegister(),
                                        local.getName(),
                                        local.getType(),
                                        local.getSignature()));
                case LineNumber line ->
                        debug.add(new ImmutableLineNumber(at, line.getLineNumber()));
                case PrologueEnd _ -> debug.add(new ImmutablePrologueEnd(at));
                case EpilogueBegin _ -> debug.add(new ImmutableEpilogueBegin(at));
                case SetSourceFile source ->
                        debug.add(new ImmutableSetSourceFile(at, source.getSourceFile()));
                default -> require(false);
            }
        }

        return new ImmutableMethodImplementation(
                body.getRegisterCount(), body.getInstructions(), body.getTryBlocks(), debug);
    }

    private static Method guardedReceiver(Method method) {
        var body = new MutableMethodImplementation(method.getImplementation());
        var wifiPresent = body.newLabelForAddress(0xd3);
        var wifiNext = body.newLabelForAddress(0xd7);
        var ethernetPresent = body.newLabelForAddress(0xe3);
        var ethernetNext = body.newLabelForAddress(0xe7);
        int address = 0;
        var indexes = new ArrayList<Integer>();

        for (int index = 0; index < body.getInstructions().size(); index++) {
            if (address == 0xd3 || address == 0xe3) {
                indexes.add(index);
            }

            address += body.getInstructions().get(index).getCodeUnits();
        }

        require(indexes.size() == 2);
        var disconnected =
                new ImmutableFieldReference(
                        "Landroid/net/NetworkInfo$State;",
                        "DISCONNECTED",
                        "Landroid/net/NetworkInfo$State;");

        for (int query = 1; query >= 0; query--) {
            int index = indexes.get(query);
            body.addInstruction(
                    index,
                    new BuilderInstruction21t(
                            Opcode.IF_NEZ, 4, query == 0 ? wifiPresent : ethernetPresent));
            body.addInstruction(
                    index + 1,
                    new BuilderInstruction21c(
                            Opcode.SGET_OBJECT, query == 0 ? 1 : 0, disconnected));
            body.addInstruction(
                    index + 2,
                    new BuilderInstruction10t(Opcode.GOTO, query == 0 ? wifiNext : ethernetNext));
        }

        return new ImmutableMethod(
                method.getDefiningClass(),
                method.getName(),
                method.getParameters(),
                method.getReturnType(),
                method.getAccessFlags(),
                method.getAnnotations(),
                method.getHiddenApiRestrictions(),
                receiverBody(method, body));
    }

    private static void guardClasses(Map<String, ClassDef> classes) {
        ClassDef selected = classes.get(OWNER);
        var direct = new ArrayList<Method>();
        int replaced = 0;

        for (Method method : selected.getDirectMethods()) {
            if (target(method)) {
                direct.add(guarded(method));
                replaced++;
            } else {
                direct.add(method);
            }
        }

        require(replaced == 1);
        classes.put(
                OWNER,
                new ImmutableClassDef(
                        selected.getType(),
                        selected.getAccessFlags(),
                        selected.getSuperclass(),
                        selected.getInterfaces(),
                        selected.getSourceFile(),
                        selected.getAnnotations(),
                        selected.getStaticFields(),
                        selected.getInstanceFields(),
                        direct,
                        selected.getVirtualMethods()));
        ClassDef receiverType = classes.get(RECEIVER);
        require(receiverType != null);
        var virtual = new ArrayList<Method>();
        int receiverReplaced = 0;

        for (Method candidate : receiverType.getVirtualMethods()) {
            if (candidate.getName().equals("onReceive")) {
                virtual.add(guardedReceiver(candidate));
                receiverReplaced++;
            } else {
                virtual.add(candidate);
            }
        }

        require(receiverReplaced == 1);
        classes.put(
                RECEIVER,
                new ImmutableClassDef(
                        receiverType.getType(),
                        receiverType.getAccessFlags(),
                        receiverType.getSuperclass(),
                        receiverType.getInterfaces(),
                        receiverType.getSourceFile(),
                        receiverType.getAnnotations(),
                        receiverType.getStaticFields(),
                        receiverType.getInstanceFields(),
                        receiverType.getDirectMethods(),
                        virtual));
    }

    /** Applies the same nullable boundary inside the API28 method-edit pipeline. */
    static void apply(
            Map<String, Object> constants, int api, String originalSha256, DexClasses classes) {
        require(api == 28 && StepConstants.admits(constants, originalSha256));
        require(classes.entry(OWNER) != null && classes.entry(RECEIVER) != null);
        var selected = new TreeMap<String, ClassDef>();
        selected.put(OWNER, classes.get(OWNER));
        selected.put(RECEIVER, classes.get(RECEIVER));
        validateDefinitions(selected.get(OWNER), selected.get(RECEIVER));
        guardClasses(selected);

        for (ClassDef changed : selected.values()) {
            classes.replace(changed);
        }
    }

    static byte[] apply(Map<String, Object> constants, int api, Path original, byte[] dex)
            throws Exception {
        validate(constants, api, original, dex);
        var classes = new TreeMap<String, ClassDef>();

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(api), dex).getClasses()) {
            require(classes.put(type.getType(), type) == null);
        }

        guardClasses(classes);
        byte[] output =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(api), classes.values()));
        var reread = new TreeMap<String, ClassDef>();

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(api), output).getClasses()) {
            require(reread.put(type.getType(), type) == null);
        }

        require(reread.keySet().equals(classes.keySet()));

        for (var entry : classes.entrySet()) {
            if (!FrameworkDexMaterializer.semantic(entry.getValue())
                    .equals(FrameworkDexMaterializer.semantic(reread.get(entry.getKey())))) {
                throw new Failure(
                        "NETWORK_CONTRACT",
                        "Materialized owner definition changed: "
                                + entry.getKey()
                                + " "
                                + FrameworkDexMaterializer.difference(
                                        entry.getValue(), reread.get(entry.getKey())),
                        3);
            }
        }

        require(StepConstants.admits(constants, FilesSupport.hash(original)));

        return output;
    }
}
