package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.RegisterRangeInstruction;
import org.jf.dexlib2.iface.instruction.ThreeRegisterInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.SortedMap;
import java.util.TreeMap;
import java.util.zip.ZipFile;

import javax.xml.parsers.DocumentBuilderFactory;

/** Serialized constructor behavior and source resource contract; no Android layout simulation. */
public final class PassionStatusBarInsetsPolicyTest {
    record Result(boolean status, boolean nav, int idCalls) {}

    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static DexBackedDexFile dex(byte[] bytes) {
        return new DexBackedDexFile(Opcodes.forApi(30), bytes);
    }

    static Method constructor(DexFile dex) {
        for (var c : dex.getClasses()) {
            if (c.getType().equals(Api30SecondaryNavigationPolicy.TYPE)) {
                for (var m : c.getDirectMethods()) {
                    if (m.getName().equals("<init>")) {
                        return m;
                    }
                }
            }
        }

        throw new AssertionError("Missing pinned constructor");
    }

    static SortedMap<Integer, Instruction> code(DexFile dex) {
        var code = new TreeMap<Integer, Instruction>();
        int address = 0;

        for (var i : constructor(dex).getImplementation().getInstructions()) {
            code.put(address, i);
            address += i.getCodeUnits();
        }

        return code;
    }

    static void primaryPath(DexFile before, DexFile after) throws Exception {
        var a = code(before);
        var b = code(after);

        check(
                FrameworkDexMaterializer.semantic(a.get(0x160))
                        .equals(FrameworkDexMaterializer.semantic(b.get(0x160))),
                "Original primary status assignment unchanged");
        var assignment = (TwoRegisterInstruction) b.get(0x160);
        check(
                b.get(0x160).getOpcode() == Opcode.IPUT_BOOLEAN
                        && assignment.getRegisterA() == 7
                        && assignment.getRegisterB() == 0
                        && ((ReferenceInstruction) b.get(0x160))
                                .getReference()
                                .toString()
                                .equals(Api30SecondaryNavigationPolicy.TYPE + "->mHasStatusBar:Z"),
                "Exact original primary true-register assignment");
        check(
                FrameworkDexMaterializer.semantic(a.get(0x15e))
                        .equals(FrameworkDexMaterializer.semantic(b.get(0x15e))),
                "Original default-display branch unchanged");
        check(
                0x15e + ((OffsetInstruction) b.get(0x15e)).getCodeOffset() == 0x18d,
                "Default-display false path still reaches original secondary entry");
        Instruction jump = b.get(0x18c);
        check(jump.getOpcode() == Opcode.GOTO, "Primary join remains unconditional");
        int join = 0x18c + ((OffsetInstruction) jump).getCodeOffset();
        Instruction next = b.get(join);
        check(
                join > 0x194
                        && next.getOpcode() == Opcode.NEW_INSTANCE
                        && ((ReferenceInstruction) next)
                                .getReference()
                                .toString()
                                .equals("Lcom/android/server/wm/RefreshRatePolicy;"),
                "Actual primary jump skips the entire inserted secondary block");
    }

    static void debugMetadata(DexFile before, DexFile after) {
        var original = new ArrayList<org.jf.dexlib2.iface.debug.DebugItem>();

        constructor(before).getImplementation().getDebugItems().forEach(original::add);
        var moved = new ArrayList<org.jf.dexlib2.iface.debug.DebugItem>();
        constructor(after).getImplementation().getDebugItems().forEach(moved::add);
        check(
                original.size() == moved.size() && original.size() > 71,
                "Exact constructor debug inventory including regression item71");

        for (int i = 0; i < original.size(); i++) {
            var a = original.get(i);
            var b = moved.get(i);
            check(
                    a.getDebugItemType() == b.getDebugItemType(),
                    "Original debug item order/type preserved");

            if (a instanceof org.jf.dexlib2.iface.debug.LocalInfo local) {
                check(
                        b instanceof org.jf.dexlib2.iface.debug.LocalInfo,
                        "Local metadata interface preserved");
                var actual = (org.jf.dexlib2.iface.debug.LocalInfo) b;
                check(
                        Objects.equals(local.getName(), actual.getName())
                                && Objects.equals(local.getType(), actual.getType())
                                && Objects.equals(local.getSignature(), actual.getSignature()),
                        "Original nullable local name/type/signature preserved at item " + i);
            }
        }

        check(
                original.get(71) instanceof org.jf.dexlib2.iface.debug.LocalInfo
                        && ((org.jf.dexlib2.iface.debug.LocalInfo) original.get(71)).getName()
                                != null,
                "Real original item71 has inferred local name");
    }

    // IDs 0/2/7 here are hostile synthetic secondary entries, not an emulation of primary
    // constructor execution.
    static Result secondary(DexFile dex, int display, boolean decor) {
        var code = code(dex);
        int pc = 0x194, pending = decor ? 1 : 0, calls = 0;
        int[] registers = new int[20];

        registers[8] = -113;
        registers[9] = -127;
        boolean status = false;

        for (int step = 0; step < 16; step++) {
            Instruction i = code.get(pc);
            check(i != null, "Instruction boundary");
            int next = pc + i.getCodeUnits();

            switch (i.getOpcode()) {
                case MOVE_RESULT ->
                        registers[((OneRegisterInstruction) i).getRegisterA()] = pending;
                case CONST_4 ->
                        registers[((OneRegisterInstruction) i).getRegisterA()] =
                                ((NarrowLiteralInstruction) i).getNarrowLiteral();
                case IF_EQZ -> {
                    if (registers[((OneRegisterInstruction) i).getRegisterA()] == 0) {
                        next = pc + ((OffsetInstruction) i).getCodeOffset();
                    }
                }
                case IF_EQ -> {
                    var v = (TwoRegisterInstruction) i;

                    if (registers[v.getRegisterA()] == registers[v.getRegisterB()]) {
                        next = pc + ((OffsetInstruction) i).getCodeOffset();
                    }
                }
                case INVOKE_VIRTUAL_RANGE -> {
                    check(
                            ((ReferenceInstruction) i)
                                    .getReference()
                                    .toString()
                                    .equals(
                                            "Lcom/android/server/wm/DisplayContent;->getDisplayId()I"),
                            "Only canonical display identity read");
                    var range = (RegisterRangeInstruction) i;
                    check(
                            range.getStartRegister() == 19 && range.getRegisterCount() == 1,
                            "Stable p2 parameter");
                    pending = display;
                    calls++;
                }
                case IPUT_BOOLEAN -> {
                    var v = (TwoRegisterInstruction) i;
                    check(v.getRegisterB() == 0, "Actual policy owner");
                    String ref = ((ReferenceInstruction) i).getReference().toString();

                    if (ref.equals(Api30SecondaryNavigationPolicy.TYPE + "->mHasStatusBar:Z")) {
                        status = registers[v.getRegisterA()] != 0;
                    } else {
                        check(
                                ref.equals(
                                        Api30SecondaryNavigationPolicy.TYPE
                                                + "->mHasNavigationBar:Z"),
                                "Only navigation end marker");

                        return new Result(status, registers[v.getRegisterA()] != 0, calls);
                    }
                }
                default ->
                        throw new AssertionError(
                                "Unexpected secondary branch opcode " + i.getOpcode());
            }

            pc = next;
        }

        throw new AssertionError("Secondary branch did not terminate");
    }

    record Dimensions(int portrait, int landscape) {}

    // Result carrier only; heights are compared with Arrays.equals, never by record equality.
    @SuppressWarnings("ArrayRecordComponent")
    record ConfigurationResult(int[] heights, int defaultCalls, List<String> dimensionSources) {}

    static Method configuration(DexFile dex) {
        for (var c : dex.getClasses()) {
            if (c.getType().equals(Api30SecondaryNavigationPolicy.TYPE)) {
                for (var m : c.getVirtualMethods()) {
                    if (m.getName().equals("onConfigurationChanged")) {
                        return m;
                    }
                }
            }
        }

        throw new AssertionError("Missing pinned configuration");
    }

    // Execute the actual serialized status-height branch; every dimension comes from its receiver.
    // Receivers are told apart by identity: local and primary resources may hold equal values.
    @SuppressWarnings("ReferenceEquality")
    static ConfigurationResult configuration(
            DexFile dex,
            int display,
            boolean status,
            Dimensions local,
            Dimensions primary,
            int[] rotation) {
        var code = new TreeMap<Integer, Instruction>();
        int at = 0;

        for (var instruction : configuration(dex).getImplementation().getInstructions()) {
            code.put(at, instruction);
            at += instruction.getCodeUnits();
        }

        Object[] registers = new Object[10];
        registers[0] = "dead rotation";
        registers[1] = local;
        registers[2] = rotation[0];
        registers[3] = rotation[1];
        registers[4] = rotation[2];
        registers[5] = rotation[3];
        registers[9] = "owner";
        int[] heights = {-17, -19, -23, -29};
        int pc = 0x22, defaultCalls = 0;
        Object pending = null;
        var sources = new ArrayList<String>();

        for (int step = 0; step < 96; step++) {
            Instruction instruction = code.get(pc);
            check(instruction != null, "Actual configuration instruction boundary");
            int next = pc + instruction.getCodeUnits();

            switch (instruction.getOpcode()) {
                case MOVE_OBJECT -> {
                    var r = (TwoRegisterInstruction) instruction;
                    registers[r.getRegisterA()] = registers[r.getRegisterB()];
                }
                case MOVE_RESULT, MOVE_RESULT_OBJECT ->
                        registers[((OneRegisterInstruction) instruction).getRegisterA()] = pending;
                case CONST, CONST_4 ->
                        registers[((OneRegisterInstruction) instruction).getRegisterA()] =
                                ((NarrowLiteralInstruction) instruction).getNarrowLiteral();
                case IF_EQZ -> {
                    if (((Number) registers[((OneRegisterInstruction) instruction).getRegisterA()])
                                    .intValue()
                            == 0) {
                        next = pc + ((OffsetInstruction) instruction).getCodeOffset();
                    }
                }
                case IF_NE -> {
                    var r = (TwoRegisterInstruction) instruction;

                    if (!registers[r.getRegisterA()].equals(registers[r.getRegisterB()])) {
                        next = pc + ((OffsetInstruction) instruction).getCodeOffset();
                    }
                }
                case GOTO -> next = pc + ((OffsetInstruction) instruction).getCodeOffset();
                case IGET_OBJECT -> {
                    var r = (TwoRegisterInstruction) instruction;
                    check(
                            registers[r.getRegisterB()].equals("owner"),
                            "Policy field owner preserved");
                    String ref = ((ReferenceInstruction) instruction).getReference().toString();

                    if (ref.endsWith("->mNavigationBarHeightForRotationDefault:[I")) {
                        check(
                                registers[1] == local,
                                "All navigation/gesture resource reads retain original display"
                                        + " resources");

                        return new ConfigurationResult(heights, defaultCalls, List.copyOf(sources));
                    }

                    if (ref.endsWith("->mStatusBarHeightForRotation:[I")) {
                        registers[r.getRegisterA()] = heights;
                    } else if (ref.endsWith(
                            "->mDisplayContent:Lcom/android/server/wm/DisplayContent;")) {
                        registers[r.getRegisterA()] = "local display";
                    } else if (ref.endsWith(
                            "->mService:Lcom/android/server/wm/WindowManagerService;")) {
                        registers[r.getRegisterA()] = "service";
                    } else {
                        throw new AssertionError("Unexpected configuration field " + ref);
                    }
                }
                case INVOKE_VIRTUAL -> {
                    var r = (FiveRegisterInstruction) instruction;
                    Object receiver = registers[r.getRegisterC()];
                    String ref = ((ReferenceInstruction) instruction).getReference().toString();

                    if (ref.equals(Api30SecondaryNavigationPolicy.TYPE + "->hasStatusBar()Z")) {
                        check(receiver.equals("owner"), "Original status capability");
                        pending = status ? 1 : 0;
                    } else if (ref.equals(
                            "Lcom/android/server/wm/DisplayContent;->getDisplayId()I")) {
                        check(receiver.equals("local display"), "Actual display identity");
                        pending = display;
                    } else if (ref.equals(
                            "Lcom/android/server/wm/WindowManagerService;->getDefaultDisplayContentLocked()Lcom/android/server/wm/DisplayContent;")) {
                        check(receiver.equals("service"), "Actual default display owner");
                        defaultCalls++;
                        pending = "default display";
                    } else if (ref.equals(
                            "Lcom/android/server/wm/DisplayContent;->getDisplayPolicy()Lcom/android/server/wm/DisplayPolicy;")) {
                        check(receiver.equals("default display"), "Actual default display policy");
                        pending = "default policy";
                    } else if (ref.equals(
                            Api30SecondaryNavigationPolicy.TYPE
                                    + "->getCurrentUserResources()Landroid/content/res/Resources;")) {
                        check(
                                receiver.equals("default policy"),
                                "No recursive secondary resource lookup");
                        pending = primary;
                    } else if (ref.equals(
                            "Landroid/content/res/Resources;->getDimensionPixelSize(I)I")) {
                        check(
                                receiver instanceof Dimensions,
                                "Dimension receiver must be selected resources");
                        int id = ((Number) registers[r.getRegisterD()]).intValue();
                        var dims = (Dimensions) receiver;
                        check(
                                id == 0x105024a || id == 0x1050249,
                                "Only the two pinned status dimensions");
                        pending = id == 0x105024a ? dims.portrait() : dims.landscape();
                        sources.add(receiver == local ? "local" : "primary");
                    } else {
                        throw new AssertionError("Unexpected configuration call " + ref);
                    }
                }
                case APUT -> {
                    var r = (ThreeRegisterInstruction) instruction;
                    ((int[]) registers[r.getRegisterB()])
                                    [((Number) registers[r.getRegisterC()]).intValue()] =
                            ((Number) registers[r.getRegisterA()]).intValue();
                }
                default ->
                        throw new AssertionError(
                                "Unexpected configuration opcode " + instruction.getOpcode());
            }

            pc = next;
        }

        throw new AssertionError(
                "Configuration did not reach original non-status resource boundary");
    }

    static List<Instruction> nonStatusTail(DexFile dex) {
        var tail = new ArrayList<Instruction>();
        boolean found = false;

        for (var instruction : configuration(dex).getImplementation().getInstructions()) {
            if (instruction instanceof ReferenceInstruction ref
                    && ref.getReference()
                            .toString()
                            .equals(
                                    Api30SecondaryNavigationPolicy.TYPE
                                            + "->mNavigationBarHeightForRotationDefault:[I")) {
                found = true;
            }

            if (found) {
                tail.add(instruction);
            }
        }

        check(found && !tail.isEmpty(), "Original non-status configuration boundary exists");

        return tail;
    }

    static void configurationMatrix(DexFile original, DexFile actual) throws Exception {
        check(
                FrameworkDexMaterializer.semantic(nonStatusTail(original))
                        .equals(FrameworkDexMaterializer.semantic(nonStatusTail(actual))),
                "All serialized non-status resource reads, operands and branches remain unchanged");
        var secondary = new Dimensions(24, 31);
        var primary = new Dimensions(60, 60);
        int[] natural = {0, 2, 1, 3};
        check(
                !Arrays.equals(
                        configuration(original, 1, true, secondary, primary, natural).heights(),
                        new int[] {60, 60, 60, 60}),
                "RED original display-local context misses effective primary RRO");

        for (var effective : new Dimensions[] {primary, new Dimensions(73, 91)}) {
            for (int[] rotation : new int[][] {natural, {1, 3, 2, 0}}) {
                for (int id : new int[] {0, 1, 2, 7}) {
                    for (boolean status : new boolean[] {false, true}) {
                        var local = id == 0 ? effective : secondary;
                        var result = configuration(actual, id, status, local, effective, rotation);
                        var expected = new int[4];
                        var selected = id == 1 ? effective : local;

                        if (status) {
                            expected[rotation[0]] = expected[rotation[1]] = selected.portrait();
                            expected[rotation[2]] = expected[rotation[3]] = selected.landscape();
                        }

                        check(
                                Arrays.equals(result.heights(), expected),
                                "Actual serialized all-rotation status dimensions selected by"
                                        + " canonical display");
                        check(
                                result.defaultCalls() == (id == 1 && status ? 1 : 0),
                                "Default lookup only on canonical status-bearing secondary");
                        check(
                                result.dimensionSources().size() == (status ? 2 : 0),
                                "Exactly two status resource reads when capability exists");

                        if (status) {
                            check(
                                    result.dimensionSources()
                                            .equals(
                                                    List.of(
                                                            id == 1 ? "primary" : "local",
                                                            id == 1 ? "primary" : "local")),
                                    "Only status resource receivers switch");
                        }
                    }
                }
            }
        }
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), input = Path.of(args[1]);
        byte[] raw;

        try (var zip = new ZipFile(input.toFile())) {
            raw = zip.getInputStream(zip.getEntry("classes2.dex")).readAllBytes();
        }

        byte[] free = SecondaryNavigationGuard.apply(TestProfiles.SYNTHETIC, 30, input, raw);
        check(!secondary(dex(free), 1, true).status(), "Original secondary status capability RED");

        if (args.length > 2 && args[2].equals("--red")) {
            check(
                    secondary(dex(free), 1, true).status(),
                    "RED: original canonical secondary lacks status capability");

            return;
        }

        byte[] passion =
                PassionSecondaryNavigationGuard.apply(TestProfiles.SYNTHETIC, 30, input, raw);
        var actual = dex(passion);
        primaryPath(dex(free), actual);
        debugMetadata(dex(raw), actual);

        for (int id : new int[] {0, 1, 2, 7}) {
            for (boolean decor : new boolean[] {false, true}) {
                Result result = secondary(actual, id, decor);
                check(
                        result.status() == (id == 1 && decor),
                        "Finite canonical secondary branch matrix");
                check(!result.nav(), "Prior navigation suppression preserved");
                check(
                        result.idCalls() == (decor ? 1 : 0),
                        "No identity read when decorations unsupported");
            }
        }

        check(
                FrameworkDexMaterializer.semantic(dex(raw).getClasses())
                        .equals(
                                FrameworkDexMaterializer.semantic(
                                        Api30SecondaryNavigationPolicy.restorePassion(actual)
                                                .getClasses())),
                "All-class semantic inverse, handlers/debug and primary path restored");
        configurationMatrix(dex(free), actual);
        Path profileRoot =
                root.resolve(
                        "profiles/"
                                + TestProfiles.declaring(
                                        root, PassionSecondaryNavigationGuard.NAME));
        var factory = DocumentBuilderFactory.newInstance();
        factory.setFeature("http://apache.org/xml/features/disallow-doctype-decl", true);
        var xml =
                factory.newDocumentBuilder()
                        .parse(
                                profileRoot
                                        .resolve(
                                                "components/statusbar-overlay/res/values/dimens.xml")
                                        .toFile());
        var dimensions = xml.getElementsByTagName("dimen");
        check(dimensions.getLength() == 3, "Only three statusbar dimensions");
        Set<String> names = new HashSet<>();

        for (int i = 0; i < dimensions.getLength(); i++) {
            var node = dimensions.item(i);
            names.add(node.getAttributes().getNamedItem("name").getNodeValue());
            check(
                    node.getTextContent().strip().equals("60px"),
                    "Pixel unit is invariant under density");
        }

        check(
                names.equals(
                        Set.of(
                                "status_bar_height",
                                "status_bar_height_portrait",
                                "status_bar_height_landscape")),
                "All portrait/upside-down/landscape/seascape aliases covered");
        var profile =
                Json.object(Json.parse(Files.readString(profileRoot.resolve("profile.json"))));
        var configure = Json.obj(profile, "configure");
        check(
                Json.str(Json.obj(Json.obj(configure, "settings"), "global"), "policy_control")
                        .equals("immersive.navigation=*"),
                "Forced status fullscreen explicitly removed while stock navigation hide retained");
        check(
                Json.list(configure.get("overlays"))
                        .contains("com.voyemu.output.overlay.passion.statusbar"),
                "Fresh candidate explicit overlay enable");
        int entries = 0;

        for (Object value : Json.list(profile.get("install"))) {
            var row = Json.object(value);

            if (Json.str(row, "artifact").equals("component:statusbar-overlay")) {
                entries++;
                check(
                        Json.str(row, "type").equals("file")
                                && Json.str(row, "target")
                                        .equals(
                                                "/system/app/VoyemuPassionStatusbar/VoyemuPassionStatusbar.apk")
                                && Json.str(row, "mode").equals("0644"),
                        "RRO must travel in immutable system, not discarded candidate userdata");
            }
        }

        check(entries == 1, "Exactly one RRO installation");
        System.out.println(
                "Passion serialized secondary branch/primary skip/inverse and pixel"
                        + " resource/profile contract PASS; Free exact baseline retained; Android"
                        + " insets/RRO migration/runtime still pending");
    }
}
