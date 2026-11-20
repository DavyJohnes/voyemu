package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.MethodImplementation;
import org.jf.dexlib2.iface.debug.DebugItem;
import org.jf.dexlib2.iface.debug.EndLocal;
import org.jf.dexlib2.iface.debug.RestartLocal;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.debug.ImmutableEndLocal;
import org.jf.dexlib2.immutable.debug.ImmutableRestartLocal;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction10x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction35c;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Exact emulator phone entrypoints; real HFP/PBAP connection claims remain untouched. */
final class PassionPhoneGuard {
    static final String NAME = "passion-api30-phone";
    static final String APK = "5bc0f97b94009b2e5aa6cb9759ea2547ab17cdadfbb8021d5e9cb6d6c6589047";
    static final String HELPER_ROOT = "passion-api30-phone/src",
            HELPER_SOURCE = "com/voyemu/passion/phone/PassionOemPhone.java";
    static final String SERVICE = "Lcom/voyah/btphone/service/BtPhoneService;",
            BINDER = "Lcom/voyah/btphone/service/BtPhoneService$4;",
            MANAGER = "Lcom/voyah/btphone/view/dial/PhoneManagerIml;",
            HELPER = "Lcom/voyemu/passion/phone/PassionOemPhone;";
    static final String CONTACT = "Lcom/voyah/btphone/view/contact/PhoneBooksFragment;",
            KEYBOARD = "Lcom/voyah/btphone/view/dial/KeyBoardFragment;",
            WINDOW = "Lcom/voyah/btphone/view/callin/AbstractCallWindow$1;";
    static final String CONTACT_EMPTY = "Lcom/voyah/btphone/view/contact/PhoneBooksFragment$2;",
            KEYPAD = "Lcom/voyah/btphone/view/dial/KeypadFragment$KeypadClickListener;";
    static final String MORE = "Lcom/voyah/btphone/view/contact/ShowMorePhoneNumberDialog;",
            SEARCH = "Lcom/voyah/btphone/adapter/ContactSearchResultAdapter;",
            BOOK = "Lcom/voyah/btphone/adapter/PhoneBookAdapter;",
            HISTORY = "Lcom/voyah/btphone/view/calllog/DialRecordFragment;";
    static final String TELECOM_PLACE_CALL =
            "Landroid/telecom/TelecomManager;->placeCall(Landroid/net/Uri;Landroid/os/Bundle;)V";
    static final String PANEL = "Lcom/voyah/btphone/view/calllog/BtPhoneAllFragment;";
    static final Map<String, Integer> PANEL_HOOKS =
            Map.of(
                    PANEL + "->onResume()V", 0x7f,
                    PANEL + "->updateBluetoothAdapterView(I)V", 0,
                    PANEL + "->updateHfpStateView(I)V", 0);
    static final Set<String> CLASSES =
            Set.of(
                    PANEL,
                    SERVICE,
                    BINDER,
                    MANAGER,
                    CONTACT,
                    KEYBOARD,
                    WINDOW,
                    CONTACT_EMPTY,
                    KEYPAD,
                    MORE,
                    SEARCH,
                    BOOK,
                    HISTORY);
    static final Set<String> METHODS =
            Set.of(
                    PANEL + "->onResume()V",
                    PANEL + "->updateBluetoothAdapterView(I)V",
                    PANEL + "->updateHfpStateView(I)V",
                    SERVICE + "->onCreate()V",
                    SERVICE + "->onDestroy()V",
                    BINDER + "->placeCall(Ljava/lang/String;)V",
                    MANAGER + "->placeCall(Ljava/lang/String;)V",
                    CONTACT + "->initData()V",
                    CONTACT + "->lambda$initData$2$PhoneBooksFragment(Ljava/util/List;)V",
                    CONTACT + "->onResume()V",
                    CONTACT + "->setContactsViewShow(Z)V",
                    KEYBOARD + "->presentDialedNumber()V",
                    KEYBOARD
                            + "->lambda$initEvent$4$KeyBoardFragment(Lcom/voyah/btphonesdk/btphone/BtPhoneManager;Landroid/view/View;)V",
                    WINDOW + "->onServiceConnected(Lcom/voyah/btphone/view/dial/PhoneManagerIml;)V",
                    CONTACT_EMPTY + "->checkIsEmpter()V",
                    KEYPAD + "->onTouch(Landroid/view/View;Landroid/view/MotionEvent;)Z",
                    MORE + "->placeCall(Ljava/lang/String;)V",
                    SEARCH + "->callPhone(Ljava/lang/String;)V",
                    BOOK + "->callPhone(Ljava/lang/String;)V",
                    HISTORY + "->placeCall(Ljava/lang/String;)V",
                    CONTACT + "->placeCall(Ljava/lang/String;)V");
    // Only data/control gates; independent log-only Bluetooth queries keep their actual values.
    static final Map<String, List<Integer>> GATES =
            Map.of(
                    CONTACT + "->initData()V",
                    List.of(68, 78, 86, 170, 178, 214, 226),
                    CONTACT + "->lambda$initData$2$PhoneBooksFragment(Ljava/util/List;)V",
                    List.of(10, 22, 30, 116, 134, 185, 193, 302, 521, 783, 791),
                    CONTACT + "->onResume()V",
                    List.of(9, 17, 25, 31, 234),
                    CONTACT + "->setContactsViewShow(Z)V",
                    List.of(4, 12),
                    KEYBOARD + "->presentDialedNumber()V",
                    List.of(151),
                    KEYBOARD
                            + "->lambda$initEvent$4$KeyBoardFragment(Lcom/voyah/btphonesdk/btphone/BtPhoneManager;Landroid/view/View;)V",
                    List.of(7),
                    WINDOW + "->onServiceConnected(Lcom/voyah/btphone/view/dial/PhoneManagerIml;)V",
                    List.of(20),
                    CONTACT_EMPTY + "->checkIsEmpter()V",
                    List.of(6, 122),
                    KEYPAD + "->onTouch(Landroid/view/View;Landroid/view/MotionEvent;)Z",
                    List.of(15));
    static final Map<String, Integer> OUTGOING =
            Map.of(
                    MORE + "->placeCall(Ljava/lang/String;)V",
                    108,
                    SEARCH + "->callPhone(Ljava/lang/String;)V",
                    124,
                    BOOK + "->callPhone(Ljava/lang/String;)V",
                    113,
                    HISTORY + "->placeCall(Ljava/lang/String;)V",
                    134,
                    CONTACT + "->placeCall(Ljava/lang/String;)V",
                    108,
                    KEYBOARD
                            + "->lambda$initEvent$4$KeyBoardFragment(Lcom/voyah/btphonesdk/btphone/BtPhoneManager;Landroid/view/View;)V",
                    219);

    static void need(boolean value) {
        Failure.require(
                value,
                "PASSION_PHONE_CONTRACT",
                "Exact owner-pinned Passion synthetic Telecom entrypoints required",
                3);
    }

    static byte[] dialerManifest(byte[] original, boolean librariesApplied) throws Exception {
        int expectedLength = librariesApplied ? 9876 : 9372;

        need(original != null && original.length == expectedLength);
        // Android 11 qualifies both bare and tel: DIAL. Reuse existing binary XML nodes for a
        // separate bare filter on MainActivity, leaving its original tel: filter in place.
        // The earlier role recipe expands the string pool by 184 bytes before the activity.
        int offset = librariesApplied ? 184 : 0;
        int activityEnd = 7896 + offset;
        byte[] result = new byte[original.length + 220];
        System.arraycopy(original, 0, result, 0, activityEnd);
        System.arraycopy(original, 7436 + offset, result, activityEnd, 36); // intent-filter start
        System.arraycopy(
                original, 7632 + offset, result, activityEnd + 36, 160); // DIAL and DEFAULT
        System.arraycopy(
                original, 7872 + offset, result, activityEnd + 196, 24); // intent-filter end
        System.arraycopy(
                original, activityEnd, result, activityEnd + 220, original.length - activityEnd);
        ByteBuffer xml = ByteBuffer.wrap(result).order(ByteOrder.LITTLE_ENDIAN);
        xml.putInt(4, result.length);
        // Telecom classifies CAR_MODE_UI before considering the default dialer UI.
        int carModeValue = 8868 + offset;
        need(result[carModeValue - 1] == 0x12 && xml.getInt(carModeValue) == -1);
        xml.putInt(carModeValue, 0);

        return result;
    }

    static boolean target(Method m) {
        return METHODS.contains(m.toString());
    }

    static void validateHelperSource(Path root) throws Exception {
        var files = FilesSupport.tree(root);

        need(files.keySet().equals(Set.of(HELPER_SOURCE)));
    }

    static void validate(String profile, int api, Path apk, byte[] bytes) throws Exception {
        need(api == 30 && FilesSupport.hash(apk).equals(APK) && bytes != null);
        int count = 0, classes = 0;

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), bytes).getClasses()) {
            if (CLASSES.contains(c.getType())) {
                classes++;

                for (var m : c.getMethods()) {
                    if (target(m)) {
                        need(m.getImplementation() != null);
                        count++;
                    }
                }
            }
        }

        need(classes == CLASSES.size() && count == METHODS.size());
    }

    static MethodImplementation route(Method m) {
        var body = m.getImplementation();

        if (GATES.containsKey(m.toString()) || OUTGOING.containsKey(m.toString())) {
            return routedCalls(m, body);
        }

        if (m.getName().equals("placeCall")) {
            return new ImmutableMethodImplementation(
                    body.getRegisterCount(),
                    List.of(
                            new ImmutableInstruction35c(
                                    Opcode.INVOKE_STATIC,
                                    1,
                                    body.getRegisterCount() - 1,
                                    0,
                                    0,
                                    0,
                                    0,
                                    new ImmutableMethodReference(
                                            HELPER,
                                            "placeCall",
                                            List.of("Ljava/lang/String;"),
                                            "V")),
                            new ImmutableInstruction10x(Opcode.RETURN_VOID)),
                    List.of(),
                    List.of());
        }

        var builder = new MutableMethodImplementation(body);
        Integer panelOffset = PANEL_HOOKS.get(m.toString());

        if (panelOffset != null) {
            addPanelHook(m, body, builder, panelOffset);
        } else {
            addLifecycleHook(m, body, builder);
        }

        // dexlib2's builder omits EndLocal/RestartLocal source metadata; preserve it explicitly.
        var debug = preservedDebugItems(body, builder);

        return new ImmutableMethodImplementation(
                builder.getRegisterCount(),
                builder.getInstructions(),
                builder.getTryBlocks(),
                debug);
    }

    private static MethodImplementation routedCalls(Method m, MethodImplementation body) {
        var gates = GATES.getOrDefault(m.toString(), List.of());
        var code = new ArrayList<Instruction>();
        int pc = 0, count = 0, outgoing = 0;

        for (var item : body.getInstructions()) {
            if (gates.contains(pc)) {
                item = gateCall(item);
                count++;
            } else if (Integer.valueOf(pc).equals(OUTGOING.get(m.toString()))) {
                item = outgoingCall(item);
                outgoing++;
            }

            code.add(item);
            pc += item.getCodeUnits();
        }

        need(count == gates.size() && outgoing == (OUTGOING.containsKey(m.toString()) ? 1 : 0));

        return new ImmutableMethodImplementation(
                body.getRegisterCount(), code, body.getTryBlocks(), body.getDebugItems());
    }

    private static Instruction gateCall(Instruction item) {
        need(item.getOpcode() == Opcode.INVOKE_VIRTUAL && item instanceof ReferenceInstruction);
        String ref = ((ReferenceInstruction) item).getReference().toString();
        boolean sync =
                ref.equals("Lcom/voyah/btphonesdk/btphone/BtPhoneManager;->getContactSyncState()I");
        need(
                sync
                        || ref.equals(
                                "Lcom/voyah/btphonesdk/btphone/BtPhoneManager;->isHfpConnected()Z")
                        || ref.equals(
                                "Lcom/voyah/btphone/manager/BluetoothStateManager;->isHfpConnected()Z")
                        || ref.equals(
                                "Lcom/voyah/btphone/manager/BluetoothStateManager;->getBluetoothConnectState()Z")
                        || ref.equals(
                                "Lcom/voyah/btphone/manager/BluetoothStateManager;->isDeviceConnect()Z")
                        || ref.equals(
                                "Lcom/voyah/btphone/manager/BluetoothStateManager;->isPbapConnected(Ljava/lang/String;)Z"));

        return new ImmutableInstruction35c(
                Opcode.INVOKE_STATIC,
                0,
                0,
                0,
                0,
                0,
                0,
                new ImmutableMethodReference(
                        HELPER,
                        sync ? "syntheticContactSyncState" : "syntheticAvailable",
                        List.of(),
                        sync ? "I" : "Z"));
    }

    private static Instruction outgoingCall(Instruction item) {
        need(
                item.getOpcode() == Opcode.INVOKE_VIRTUAL
                        && item instanceof ReferenceInstruction referenceInstruction
                        && referenceInstruction.getReference().toString().equals(TELECOM_PLACE_CALL)
                        && item instanceof FiveRegisterInstruction fiveRegisterInstruction
                        && fiveRegisterInstruction.getRegisterCount() == 3);
        int uri = ((FiveRegisterInstruction) item).getRegisterD();

        return new ImmutableInstruction35c(
                Opcode.INVOKE_STATIC,
                1,
                uri,
                0,
                0,
                0,
                0,
                new ImmutableMethodReference(
                        HELPER, "placeCall", List.of("Landroid/net/Uri;"), "V"));
    }

    private static void addPanelHook(
            Method m,
            MethodImplementation body,
            MutableMethodImplementation builder,
            int panelOffset) {
        int index = 0;
        int pc = 0;

        while (pc < panelOffset) {
            pc += builder.getInstructions().get(index++).getCodeUnits();
        }

        need(pc == panelOffset);
        var original = builder.newLabelForIndex(index);
        int receiver = body.getRegisterCount() - m.getParameters().size() - 1;
        builder.addInstruction(
                index++,
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        1,
                        receiver,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                HELPER, "showSyntheticPanel", List.of("Ljava/lang/Object;"), "Z")));
        builder.addInstruction(index++, new BuilderInstruction11x(Opcode.MOVE_RESULT, 0));
        builder.addInstruction(index++, new BuilderInstruction21t(Opcode.IF_EQZ, 0, original));
        builder.addInstruction(index, new BuilderInstruction10x(Opcode.RETURN_VOID));
    }

    private static void addLifecycleHook(
            Method m, MethodImplementation body, MutableMethodImplementation builder) {
        boolean start = m.getName().equals("onCreate");
        int index = start ? builder.getInstructions().size() - 1 : 0;

        need(!start || builder.getInstructions().get(index).getOpcode() == Opcode.RETURN_VOID);
        builder.addInstruction(
                index,
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        start ? 1 : 0,
                        start ? body.getRegisterCount() - 1 : 0,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                HELPER,
                                start ? "start" : "stop",
                                start ? List.of("Landroid/content/Context;") : List.of(),
                                "V")));
    }

    private static List<DebugItem> preservedDebugItems(
            MethodImplementation body, MutableMethodImplementation builder) {
        var debug = new ArrayList<DebugItem>();
        var old = body.getDebugItems().iterator();

        for (var item : builder.getDebugItems()) {
            need(old.hasNext());
            var before = old.next();
            need(before.getDebugItemType() == item.getDebugItemType());

            if (item instanceof EndLocal e && before instanceof EndLocal p) {
                debug.add(
                        new ImmutableEndLocal(
                                item.getCodeAddress(),
                                e.getRegister(),
                                p.getName(),
                                p.getType(),
                                p.getSignature()));
            } else if (item instanceof RestartLocal e && before instanceof RestartLocal p) {
                debug.add(
                        new ImmutableRestartLocal(
                                item.getCodeAddress(),
                                e.getRegister(),
                                p.getName(),
                                p.getType(),
                                p.getSignature()));
            } else {
                debug.add(item);
            }
        }

        need(!old.hasNext());

        return debug;
    }

    static ImmutableDexFile rewrite(DexFile dex, Map<String, MethodImplementation> restore) {
        var classes = new ArrayList<ClassDef>();
        int count = 0;

        for (var c : dex.getClasses()) {
            if (!CLASSES.contains(c.getType())) {
                classes.add(c);
                continue;
            }

            var partitions = new ArrayList<List<Method>>();

            for (var partition : List.of(c.getDirectMethods(), c.getVirtualMethods())) {
                var methods = new ArrayList<Method>();

                for (var m : partition) {
                    if (target(m)) {
                        MethodImplementation body =
                                restore == null ? route(m) : restore.get(m.toString());
                        need(body != null);
                        m =
                                new ImmutableMethod(
                                        m.getDefiningClass(),
                                        m.getName(),
                                        m.getParameters(),
                                        m.getReturnType(),
                                        m.getAccessFlags(),
                                        m.getAnnotations(),
                                        m.getHiddenApiRestrictions(),
                                        body);
                        count++;
                    }

                    methods.add(m);
                }

                partitions.add(methods);
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
                            partitions.get(0),
                            partitions.get(1)));
        }

        need(count == METHODS.size());

        return new ImmutableDexFile(Opcodes.forApi(30), classes);
    }

    static byte[] apply(String profile, int api, Path apk, byte[] bytes) throws Exception {
        validate(profile, api, apk, bytes);
        var before = new DexBackedDexFile(Opcodes.forApi(30), bytes);
        var originals = new HashMap<String, MethodImplementation>();

        for (var c : before.getClasses()) {
            for (var m : c.getMethods()) {
                if (target(m)) {
                    originals.put(m.toString(), m.getImplementation());
                }
            }
        }

        var expected = rewrite(before, null);
        byte[] out = FrameworkDexMaterializer.serialize(expected);
        var actual = new DexBackedDexFile(Opcodes.forApi(30), out);
        need(
                FrameworkDexMaterializer.semantic(expected.getClasses())
                        .equals(FrameworkDexMaterializer.semantic(actual.getClasses())));
        need(
                FrameworkDexMaterializer.semantic(before.getClasses())
                        .equals(
                                FrameworkDexMaterializer.semantic(
                                        rewrite(actual, originals).getClasses())));

        return out;
    }
}
