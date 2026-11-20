package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21c;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.MethodImplementation;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;
import org.jf.dexlib2.iface.instruction.WideLiteralInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableExceptionHandler;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.ImmutableTryBlock;
import org.jf.dexlib2.immutable.debug.ImmutableLineNumber;
import org.jf.dexlib2.immutable.debug.ImmutableStartLocal;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction10x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction21c;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction21t;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction31c;
import org.jf.dexlib2.immutable.reference.ImmutableStringReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;

/** Actual OEM power gateway: preserve Bluetooth and the genuine automotive MAC retransmission. */
public final class Api30SystemPolicyPowerGuardTest {
    static final String OWNER = "Lcom/qinggan/systempolicy/bluetooth/BluetoothSource;";
    static final String LISTENER =
            "Lcom/qinggan/systempolicy/bluetooth/BluetoothSource$PowerStateListener;";
    static final String INIT = OWNER + "->initPowerMgr()V";
    static final String SERVICE_CHECK =
            "Landroid/os/ServiceManager;->checkService(Ljava/lang/String;)Landroid/os/IBinder;";

    /** Pins of the API30 SystemPolicy originals. */
    static final List<String> PINS =
            List.of(
                    "02749c34c515b8dd2ecdb6f5030762c292bc47fc3b79214d7965cb5cf56226eb",
                    "a81710fcbb382d9a1c449b240c2dc40aebc3d2e3cc309339ec1be3c5fbb2de00");

    static final String API28_PIN =
            "6c223315e64d14f9f1c715c42474257df772ce218e40094cf91b1cb012b2fadc";

    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static Method method(DexClasses classes, String owner, String name) {
        Method result = null;

        for (var candidate : classes.get(owner).getMethods()) {
            if (candidate.getName().equals(name)) {
                check(result == null, "one exact OEM method: " + owner + name);
                result = candidate;
            }
        }

        check(result != null && result.getImplementation() != null, "OEM method has code");

        return result;
    }

    static Map<String, String> inventory(DexClasses classes, Path apk) throws Exception {
        return inventory(classes, apk, false);
    }

    static Map<String, String> inventory(DexClasses classes, Path apk, boolean encoding)
            throws Exception {
        Map<String, String> result = new LinkedHashMap<>();

        for (String entry : NativeCore.entries(apk).keySet()) {
            if (!entry.matches("classes[0-9]*\\.dex")) {
                continue;
            }

            for (String owner : classes.classes(entry)) {
                var type = classes.get(owner);
                result.put(
                        owner + "#metadata",
                        FrameworkDexMaterializer.semantic(
                                List.of(
                                        type.getType(),
                                        type.getAccessFlags(),
                                        Objects.toString(type.getSuperclass(), ""),
                                        type.getInterfaces(),
                                        Objects.toString(type.getSourceFile(), ""),
                                        type.getAnnotations(),
                                        type.getStaticFields(),
                                        type.getInstanceFields())));

                for (var candidate : type.getMethods()) {
                    result.put(
                            DexEdits.descriptor(candidate),
                            FrameworkDexMaterializer.semantic(
                                    encoding ? normalize(candidate) : candidate));
                }
            }
        }

        return result;
    }

    static void retained(DexClasses before, DexClasses after, Path apk, boolean api28)
            throws Exception {
        retained(before, after, apk, api28, false);
    }

    static void retained(
            DexClasses before, DexClasses after, Path apk, boolean api28, boolean encoding)
            throws Exception {
        var original = inventory(before, apk, encoding);
        var patched = inventory(after, apk, encoding);

        check(original.containsKey(INIT) || api28, "actual OEM method inventory is nonempty");
        check(original.keySet().equals(patched.keySet()), "no method or class added or removed");

        for (var entry : original.entrySet()) {
            String descriptor = entry.getKey();
            boolean adapterGuard =
                    descriptor.equals(
                                    SystemPolicyHardwareGuard.MANAGER
                                            + "-><init>(Landroid/content/Context;)V")
                            || descriptor.equals(
                                    SystemPolicyHardwareGuard.SERVICE
                                            + "->startChargingService()V");

            if (!adapterGuard
                    && (api28 || !descriptor.equals(INIT))
                    && !entry.getValue().equals(patched.get(descriptor))) {
                check(
                        entry.getValue().equals(patched.get(descriptor)),
                        "unrelated OEM method changed: "
                                + descriptor
                                + ": "
                                + difference(before, after, descriptor));
            }
        }
    }

    static String difference(DexClasses before, DexClasses after, String descriptor)
            throws Exception {
        if (!descriptor.contains("->")) {
            return "class metadata differs";
        }

        String owner = descriptor.substring(0, descriptor.indexOf("->"));

        for (var left : before.get(owner).getMethods()) {
            if (DexEdits.descriptor(left).equals(descriptor)) {
                for (var right : after.get(owner).getMethods()) {
                    if (DexEdits.descriptor(right).equals(descriptor)) {
                        return FrameworkDexMaterializer.difference(left, right);
                    }
                }
            }
        }

        return "class metadata differs";
    }

    /**
     * DEX string indexes may widen encoding; compare every logical instruction and debug target.
     */
    static Method normalize(Method method) {
        if (method.getImplementation() == null) {
            return method;
        }

        var code = new MutableMethodImplementation(method.getImplementation());

        for (int i = 0; i < code.getInstructions().size(); i++) {
            var instruction = code.getInstructions().get(i);

            if (instruction.getOpcode() == Opcode.CONST_STRING_JUMBO) {
                code.replaceInstruction(
                        i,
                        new BuilderInstruction21c(
                                Opcode.CONST_STRING,
                                ((OneRegisterInstruction) instruction).getRegisterA(),
                                ((ReferenceInstruction) instruction).getReference()));
            }
        }

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

    static Method encodingFixture(boolean jumbo) {
        var text = new ImmutableStringReference("same logical text");
        List<Instruction> instructions =
                List.of(
                        jumbo
                                ? new ImmutableInstruction31c(Opcode.CONST_STRING_JUMBO, 0, text)
                                : new ImmutableInstruction21c(Opcode.CONST_STRING, 0, text),
                        new ImmutableInstruction21t(Opcode.IF_EQZ, 1, 3),
                        new ImmutableInstruction10x(Opcode.NOP),
                        new ImmutableInstruction10x(Opcode.RETURN_VOID));
        int first = jumbo ? 3 : 2, end = jumbo ? 6 : 5;
        var code =
                new ImmutableMethodImplementation(
                        2,
                        instructions,
                        List.of(
                                new ImmutableTryBlock(
                                        0, end, List.of(new ImmutableExceptionHandler(null, end)))),
                        List.of(
                                new ImmutableLineNumber(0, 100),
                                new ImmutableStartLocal(
                                        first, 0, "text", "Ljava/lang/String;", null),
                                new ImmutableLineNumber(end, 200)));

        return new ImmutableMethod(
                "Lfixture/Encoding;",
                "example",
                List.of(),
                "V",
                1,
                java.util.Set.of(),
                java.util.Set.of(),
                code);
    }

    static void encodingNormalization() throws Exception {
        check(
                FrameworkDexMaterializer.semantic(normalize(encodingFixture(true)))
                        .equals(FrameworkDexMaterializer.semantic(encodingFixture(false))),
                "encoding normalization preserves string register/reference, branch, try/handler"
                        + " and all debug data");
    }

    /** Executes only the typed instructions of the real OEM gateway and its power callback. */
    static final class Machine {
        // Plain Object sentinels retain receiver identity through Object.equals.
        final Object self = new Object(), context = new Object(), binder = new Object();
        final Object car = new Object(), manager = new Object(), listener = new Object();
        final Map<String, Object> fields = new HashMap<>();
        final List<String> calls = new ArrayList<>();
        final boolean available, managerAvailable;
        boolean adapterAvailable = true;
        Object intent;
        int launches;
        Object lifecycle;
        boolean alreadySend = true;
        int sends;

        Machine(boolean available, boolean managerAvailable) {
            this.available = available;
            this.managerAvailable = managerAvailable;
            fields.put("mContext", context);
            fields.put("mCarPowerStateListener", listener);
            fields.put("this$0", self);
        }

        static String reference(Instruction instruction) {
            return ((ReferenceInstruction) instruction).getReference().toString();
        }

        static String field(Instruction instruction) {
            String text = reference(instruction);

            return text.substring(text.indexOf("->") + 2, text.indexOf(':'));
        }

        Object createCar(Object[] values) {
            check(
                    values.length == 5
                            && Objects.equals(values[0], context)
                            && Objects.equals(values[1], 0)
                            && Objects.equals(values[2], 0L)
                            && Objects.equals(values[4], lifecycle),
                    "genuine createCar context, null handler, zero timeout and lifecycle");
            calls.add("createCar");

            return car;
        }

        static boolean chargingCall(String target) {
            return target.startsWith("Landroid/bluetooth/BluetoothAdapter;")
                    || target.startsWith("Landroid/content/Intent;")
                    || target.startsWith(SystemPolicyHardwareGuard.SERVICE);
        }

        Object invokeCharging(String target, Object[] values) {
            if (target.equals(
                    "Landroid/bluetooth/BluetoothAdapter;->getDefaultAdapter()Landroid/bluetooth/BluetoothAdapter;")) {
                check(values.length == 0, "original adapter probe");
                calls.add("adapter");

                return adapterAvailable ? new Object() : null;
            }

            if (target.equals("Landroid/content/Intent;-><init>()V")) {
                check(
                        values.length == 1 && Objects.equals(values[0], intent),
                        "actual intent receiver");
                calls.add("intent");

                return null;
            }

            if (target.equals(
                    SystemPolicyHardwareGuard.SERVICE + "->getPackageName()Ljava/lang/String;")) {
                check(
                        values.length == 1 && Objects.equals(values[0], self),
                        "actual service receiver");

                return "com.qinggan.systempolicy";
            }

            if (target.equals(
                    "Landroid/content/Intent;->setClassName(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;")) {
                check(
                        values.length == 3
                                && Objects.equals(values[0], intent)
                                && values[1].equals("com.qinggan.systempolicy")
                                && values[2].equals(
                                        "com.qinggan.systempolicy.bluetooth.chargestation.service.ChargingService"),
                        "exact OEM explicit charging intent");
                calls.add("className");

                return intent;
            }

            if (target.equals(
                    SystemPolicyHardwareGuard.SERVICE
                            + "->startService(Landroid/content/Intent;)Landroid/content/ComponentName;")) {
                check(
                        values.length == 2
                                && Objects.equals(values[0], self)
                                && Objects.equals(values[1], intent),
                        "OEM starts captured charging intent");
                calls.add("startService");
                launches++;

                return null;
            }

            throw new AssertionError("Unexpected charging invocation: " + target);
        }

        Object invoke(Instruction instruction, Object[] registers) {
            var args = (FiveRegisterInstruction) instruction;
            int[] positions = {
                args.getRegisterC(),
                args.getRegisterD(),
                args.getRegisterE(),
                args.getRegisterF(),
                args.getRegisterG()
            };
            Object[] values = new Object[args.getRegisterCount()];

            for (int i = 0; i < values.length; i++) {
                values[i] = registers[positions[i]];
            }

            String target = reference(instruction);

            if (chargingCall(target)) {
                return invokeCharging(target, values);
            }

            if (target.equals(SERVICE_CHECK)) {
                check(
                        values.length == 1 && values[0].equals("car_service"),
                        "real automotive service lookup");
                calls.add("checkService");

                return available ? binder : null;
            }

            if (target.contains("BluetoothSource$16;-><init>")) {
                check(
                        values.length == 2 && Objects.equals(values[1], self),
                        "genuine lifecycle receives owner");
                lifecycle = values[0];
                calls.add("lifecycle");

                return null;
            }

            if (target.equals(
                    "Landroid/car/Car;->createCar(Landroid/content/Context;Landroid/os/Handler;JLandroid/car/Car$CarServiceLifecycleListener;)Landroid/car/Car;")) {
                return createCar(values);
            }

            if (target.equals(
                    "Landroid/car/Car;->getCarManager(Ljava/lang/String;)Ljava/lang/Object;")) {
                check(
                        values.length == 2
                                && Objects.equals(values[0], car)
                                && values[1].equals("power"),
                        "real car power manager query");
                calls.add("getCarManager");

                return managerAvailable ? manager : null;
            }

            if (target.equals(
                    "Landroid/car/hardware/power/CarPowerManager;->setListener(Landroid/car/hardware/power/CarPowerManager$CarPowerStateListener;)V")) {
                if (values[0] == null) {
                    throw new NullPointerException("present CarService returned no power manager");
                }

                check(
                        values.length == 2
                                && Objects.equals(values[0], manager)
                                && Objects.equals(values[1], listener),
                        "genuine manager and listener identity");
                calls.add("setListener");

                return null;
            }

            if (target.equals(OWNER + "->access$100(" + OWNER + ")V")) {
                check(
                        values.length == 1 && Objects.equals(values[0], self),
                        "power callback retransmits own MAC");
                sends++;
                calls.add("sendMac");

                return null;
            }

            throw new AssertionError("Unexpected OEM invocation: " + target);
        }

        void run(Method method, boolean powerCallback, int state) {
            var body = new Api30NetworkNullGuardTest.Body(method);
            Object[] registers = new Object[method.getImplementation().getRegisterCount()];

            registers[registers.length - (powerCallback ? 2 : 1)] = self;

            if (powerCallback) {
                registers[registers.length - 1] = state;
            }

            Object result = null;
            int index = 0;

            for (int steps = 0; steps < 100; steps++) {
                Instruction instruction = body.instructions().get(index);
                int next = index + 1;
                int a = instruction instanceof OneRegisterInstruction r ? r.getRegisterA() : -1;
                int b = instruction instanceof TwoRegisterInstruction r ? r.getRegisterB() : -1;

                switch (instruction.getOpcode()) {
                    case CONST_STRING ->
                            registers[a] =
                                    ((org.jf.dexlib2.iface.reference.StringReference)
                                                    ((ReferenceInstruction) instruction)
                                                            .getReference())
                                            .getString();
                    case CONST_4 ->
                            registers[a] =
                                    ((NarrowLiteralInstruction) instruction).getNarrowLiteral();
                    case CONST_WIDE_16 ->
                            registers[a] = ((WideLiteralInstruction) instruction).getWideLiteral();
                    case NEW_INSTANCE -> {
                        registers[a] = new Object();

                        if (reference(instruction).equals("Landroid/content/Intent;")) {
                            check(intent == null, "only one charging intent");
                            intent = registers[a];
                        }
                    }
                    case IGET_OBJECT -> {
                        check(Objects.equals(registers[b], self), "OEM field receiver");
                        registers[a] = fields.get(field(instruction));
                    }
                    case IPUT_OBJECT -> {
                        check(Objects.equals(registers[b], self), "OEM field write receiver");
                        fields.put(field(instruction), registers[a]);
                    }
                    case IPUT_BOOLEAN -> {
                        check(
                                field(instruction).equals("alreadySend")
                                        && Objects.equals(registers[b], self),
                                "only actual retransmit latch reset");
                        alreadySend = !Objects.equals(registers[a], 0);
                    }
                    case CHECK_CAST ->
                            check(
                                    registers[a] == null || Objects.equals(registers[a], manager),
                                    "actual power manager type");
                    case INVOKE_STATIC, INVOKE_DIRECT, INVOKE_VIRTUAL ->
                            result = invoke(instruction, registers);
                    case MOVE_RESULT_OBJECT -> registers[a] = result;
                    case IF_EQZ, IF_NEZ, IF_NE -> {
                        boolean branch =
                                instruction.getOpcode() == Opcode.IF_EQZ
                                        ? registers[a] == null || Objects.equals(registers[a], 0)
                                        : instruction.getOpcode() == Opcode.IF_NEZ
                                                ? registers[a] != null
                                                        && !Objects.equals(registers[a], 0)
                                                : !Objects.equals(registers[a], registers[b]);

                        if (branch) {
                            next =
                                    body.index()
                                            .get(
                                                    body.addresses().get(index)
                                                            + ((OffsetInstruction) instruction)
                                                                    .getCodeOffset());
                        }
                    }
                    case RETURN_VOID -> {
                        return;
                    }
                    default ->
                            throw new AssertionError(
                                    "Unexpected real OEM opcode: " + instruction.getOpcode());
                }

                index = next;
            }

            throw new AssertionError("OEM execution did not return");
        }
    }

    static void chargingFlow(DexClasses before, DexClasses after) {
        Method original = method(before, SystemPolicyHardwareGuard.SERVICE, "startChargingService");
        Method patched = method(after, SystemPolicyHardwareGuard.SERVICE, "startChargingService");
        var a = new Api30NetworkNullGuardTest.Body(original);
        var b = new Api30NetworkNullGuardTest.Body(patched);

        check(
                b.instructions().size() == a.instructions().size() + 8,
                "only adapter and car service guards added");
        Map<Integer, Integer> oldMap = new HashMap<>(), newMap = new HashMap<>();

        for (int i = 0; i < a.instructions().size(); i++) {
            oldMap.put(i, i);
            newMap.put(i + 8, i);
        }

        for (int i = 0; i < a.instructions().size(); i++) {
            check(
                    Api30NetworkNullGuardTest.signature(a, i, oldMap)
                            .equals(Api30NetworkNullGuardTest.signature(b, i + 8, newMap)),
                    "every original charging instruction preserved");
        }

        check(
                patched.getImplementation().getRegisterCount() == 4
                        && patched.getImplementation().getTryBlocks().isEmpty(),
                "charging register and exception contract retained");
        Machine oem = new Machine(false, true);
        oem.run(original, false, 0);
        check(oem.launches == 1, "real OEM reproduces launch without car service");

        for (boolean adapter : List.of(false, true)) {
            for (boolean available : List.of(false, true)) {
                Machine machine = new Machine(available, true);
                machine.adapterAvailable = adapter;
                machine.run(patched, false, 0);
                check(
                        machine.launches == (adapter && available ? 1 : 0),
                        "charging launch requires adapter and accessible car service");
                check(
                        (machine.intent != null) == (adapter && available),
                        "unsupported charging returns before intent allocation");
                check(
                        machine.calls.equals(
                                !adapter
                                        ? List.of("adapter")
                                        : !available
                                                ? List.of("adapter", "checkService")
                                                : List.of(
                                                        "adapter",
                                                        "checkService",
                                                        "intent",
                                                        "className",
                                                        "startService")),
                        "preserved OEM path and guard ordering");
            }
        }
    }

    static void flow(DexClasses before, DexClasses after) {
        Method original = method(before, OWNER, "initPowerMgr");
        Method patched = method(after, OWNER, "initPowerMgr");
        var a = new Api30NetworkNullGuardTest.Body(original);
        var b = new Api30NetworkNullGuardTest.Body(patched);

        check(
                b.instructions().size() == a.instructions().size() + 4,
                "exactly four narrow service guard instructions");
        Map<Integer, Integer> oldMap = new HashMap<>(), newMap = new HashMap<>();

        for (int i = 0; i < a.instructions().size(); i++) {
            oldMap.put(i, i);
            newMap.put(i + 4, i);
        }

        for (int i = 0; i < a.instructions().size(); i++) {
            check(
                    Api30NetworkNullGuardTest.signature(a, i, oldMap)
                            .equals(Api30NetworkNullGuardTest.signature(b, i + 4, newMap)),
                    "all original typed gateway instructions retained");
        }

        check(
                patched.getImplementation().getRegisterCount() == 6
                        && patched.getImplementation().getTryBlocks().isEmpty(),
                "no registers or exception suppression added");
        Machine absent = new Machine(false, true);
        absent.run(patched, false, 0);
        check(
                absent.calls.equals(List.of("checkService")) && absent.fields.get("mCar") == null,
                "absent car service skips only automotive registration");
        Machine present = new Machine(true, true);
        present.run(patched, false, 0);
        check(
                present.calls.equals(
                        List.of(
                                "checkService",
                                "lifecycle",
                                "createCar",
                                "getCarManager",
                                "setListener")),
                "genuine service-present path executes once");
        Machine broken = new Machine(true, false);
        boolean failed = false;

        try {
            broken.run(patched, false, 0);
        } catch (NullPointerException expected) {
            failed = true;
        }

        check(
                failed && broken.calls.contains("getCarManager"),
                "present service with missing manager is not success");

        for (DexClasses classes : List.of(before, after)) {
            Method callback = method(classes, LISTENER, "onStateChanged");

            for (int state : List.of(0, 1, 5, 6, 7)) {
                Machine listener = new Machine(true, true);
                listener.run(callback, true, state);
                check(
                        listener.sends == (state == 6 ? 1 : 0)
                                && listener.alreadySend == (state != 6),
                        "power state 6 alone resets and retransmits MAC");
            }

            Method bridge = method(classes, OWNER, "access$100");
            boolean sendsMac = false;

            for (var instruction : bridge.getImplementation().getInstructions()) {
                if (instruction instanceof ReferenceInstruction reference
                        && reference
                                .getReference()
                                .toString()
                                .equals(OWNER + "->sendBtMacAddressToMcu()V")) {
                    sendsMac = true;
                }
            }

            check(sendsMac, "callback bridge reaches genuine MAC transmission implementation");
        }
    }

    static void rejected(
            Map<String, Object> step,
            int api,
            String pin,
            DexClasses classes,
            Path apk,
            String code)
            throws Exception {
        var before = inventory(classes, apk);
        boolean failed = false;

        try {
            SystemPolicyHardwareGuard.apply(step, api, pin, classes);
        } catch (Failure expected) {
            check(expected.code.equals(code), "precise contract failure: " + expected.code);
            failed = true;
        }

        check(
                failed && before.equals(inventory(classes, apk)),
                "contract failure before any mutation");
    }

    static DexClasses reread(DexClasses classes, Path apk, int api) throws Exception {
        Map<String, byte[]> archive = new LinkedHashMap<>(NativeCore.entries(apk));

        for (String entry : archive.keySet()) {
            if (entry.matches("classes[0-9]*\\.dex")) {
                for (String owner : classes.classes(entry)) {
                    classes.replace(classes.get(owner));
                }
            }
        }

        classes.write(archive);

        return new DexClasses(
                archive,
                archive.keySet().stream()
                        .filter(name -> name.matches("classes[0-9]*\\.dex"))
                        .sorted()
                        .toList(),
                api);
    }

    static void replace(DexClasses classes, MethodImplementation code) {
        replace(classes, OWNER, "initPowerMgr", code);
    }

    static void replace(DexClasses classes, String owner, String name, MethodImplementation code) {
        var type = classes.get(owner);
        List<Method> virtual = new ArrayList<>();
        List<Method> direct = new ArrayList<>();

        for (Method candidate : type.getMethods()) {
            (org.jf.dexlib2.AccessFlags.STATIC.isSet(candidate.getAccessFlags())
                                    || org.jf.dexlib2.AccessFlags.PRIVATE.isSet(
                                            candidate.getAccessFlags())
                                    || candidate.getName().equals("<init>")
                            ? direct
                            : virtual)
                    .add(
                            candidate.getName().equals(name)
                                    ? new ImmutableMethod(
                                            candidate.getDefiningClass(),
                                            candidate.getName(),
                                            candidate.getParameters(),
                                            candidate.getReturnType(),
                                            candidate.getAccessFlags(),
                                            candidate.getAnnotations(),
                                            candidate.getHiddenApiRestrictions(),
                                            code)
                                    : candidate);
        }

        classes.replace(
                new ImmutableClassDef(
                        type.getType(),
                        type.getAccessFlags(),
                        type.getSuperclass(),
                        type.getInterfaces(),
                        type.getSourceFile(),
                        type.getAnnotations(),
                        type.getStaticFields(),
                        type.getInstanceFields(),
                        direct,
                        virtual));
    }

    static void checkProfile(Path root, String pin) throws Exception {
        Path apk =
                root.resolve(
                        ".local/profiles/"
                                + TestProfiles.pinning(root, pin)
                                + "/originals/SystemPolicy-release.apk");
        Map<String, Object> step = Map.of(StepConstants.PINS, List.of(pin));

        NativeCore.pinned(apk, pin);
        var before = SystemPolicyHardwareTest.classes(apk, 30);
        var after = SystemPolicyHardwareTest.classes(apk, 30);
        SystemPolicyHardwareGuard.apply(step, 30, pin, after);
        retained(before, after, apk, false);
        var canonicalBefore = reread(before, apk, 30);
        after = reread(after, apk, 30);
        chargingFlow(canonicalBefore, after);
        flow(canonicalBefore, after);
        retained(canonicalBefore, after, apk, false, true);
        rejected(step, 29, pin, before, apk, "SYSTEMPOLICY_CONTRACT");
        rejected(step, 30, "0".repeat(64), before, apk, "SYSTEMPOLICY_CONTRACT");
        rejected(Map.of(), 30, pin, before, apk, "SYSTEMPOLICY_CONTRACT");
        var changed = SystemPolicyHardwareTest.classes(apk, 30);
        changed.edit(
                List.of(
                        new DexEdits.MethodEdit(
                                INIT,
                                null,
                                List.of(
                                        DexEdits.Edit.replace(
                                                List.of("const-string v1, \"power\""),
                                                List.of("const-string v1, \"other\""))))));
        rejected(step, 30, pin, changed, apk, "SYSTEMPOLICY_POWER_CONTRACT");
        changed = SystemPolicyHardwareTest.classes(apk, 30);
        changed.edit(List.of(new DexEdits.MethodEdit(INIT, 7, List.of())));
        rejected(step, 30, pin, changed, apk, "SYSTEMPOLICY_POWER_CONTRACT");
        changed = SystemPolicyHardwareTest.classes(apk, 30);
        changed.edit(
                List.of(
                        new DexEdits.MethodEdit(
                                INIT,
                                null,
                                List.of(
                                        DexEdits.Edit.replace(
                                                List.of("return-void"), List.of("nop"))))));
        rejected(step, 30, pin, changed, apk, "SYSTEMPOLICY_POWER_CONTRACT");
        changed = SystemPolicyHardwareTest.classes(apk, 30);
        Method target = method(changed, OWNER, "initPowerMgr");
        var code = target.getImplementation();
        replace(
                changed,
                new ImmutableMethodImplementation(
                        code.getRegisterCount(),
                        code.getInstructions(),
                        List.of(
                                new ImmutableTryBlock(
                                        0, 2, List.of(new ImmutableExceptionHandler(null, 0)))),
                        code.getDebugItems()));
        rejected(step, 30, pin, changed, apk, "SYSTEMPOLICY_POWER_CONTRACT");
        String charging = SystemPolicyHardwareGuard.SERVICE + "->startChargingService()V";

        for (var edit :
                List.of(
                        new DexEdits.MethodEdit(charging, 5, List.of()),
                        new DexEdits.MethodEdit(
                                charging,
                                null,
                                List.of(
                                        DexEdits.Edit.replace(
                                                List.of(
                                                        "const-string v2,"
                                                            + " \"com.qinggan.systempolicy.bluetooth.chargestation.service.ChargingService\""),
                                                List.of("const-string v2, \"other.Service\"")))),
                        new DexEdits.MethodEdit(
                                charging,
                                null,
                                List.of(
                                        DexEdits.Edit.replace(
                                                List.of("return-void"), List.of("nop")))))) {
            changed = SystemPolicyHardwareTest.classes(apk, 30);
            changed.edit(List.of(edit));
            rejected(step, 30, pin, changed, apk, "SYSTEMPOLICY_POWER_CONTRACT");
        }

        changed = SystemPolicyHardwareTest.classes(apk, 30);
        code =
                method(changed, SystemPolicyHardwareGuard.SERVICE, "startChargingService")
                        .getImplementation();
        replace(
                changed,
                SystemPolicyHardwareGuard.SERVICE,
                "startChargingService",
                new ImmutableMethodImplementation(
                        code.getRegisterCount(),
                        code.getInstructions(),
                        List.of(
                                new ImmutableTryBlock(
                                        0, 2, List.of(new ImmutableExceptionHandler(null, 0)))),
                        code.getDebugItems()));
        rejected(step, 30, pin, changed, apk, "SYSTEMPOLICY_POWER_CONTRACT");
        rejected(step, 30, pin, after, apk, "SYSTEMPOLICY_POWER_CONTRACT");
        System.out.println(
                pin + ": absent/present/failing-manager/state6/retained/negative passed");
    }

    public static void main(String[] args) throws Exception {
        check(args.length == 1 || args.length == 2, "ROOT [SYSTEMPOLICY_PIN]");
        encodingNormalization();
        Path root = Path.of(args[0]).toRealPath();

        for (String pin : args.length == 2 ? List.of(args[1]) : PINS) {
            checkProfile(root, pin);
        }

        Path apk =
                root.resolve(
                        ".local/profiles/"
                                + TestProfiles.pinning(root, API28_PIN)
                                + "/originals/SystemPolicy-release.apk");
        var before = SystemPolicyHardwareTest.classes(apk, 28);
        var after = SystemPolicyHardwareTest.classes(apk, 28);
        SystemPolicyHardwareGuard.apply(
                Map.of(StepConstants.PINS, List.of(API28_PIN)), 28, API28_PIN, after);
        retained(before, after, apk, true);
        var canonicalBefore = reread(before, apk, 28);
        after = reread(after, apk, 28);
        retained(canonicalBefore, after, apk, true, true);
        System.out.println(
                "Api30SystemPolicyPowerGuardTest: passed; API28 unchanged outside existing adapter"
                        + " guards");
    }
}
