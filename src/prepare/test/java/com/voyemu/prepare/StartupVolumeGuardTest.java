package com.voyemu.prepare;

import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;
import org.jf.dexlib2.iface.instruction.formats.ArrayPayload;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.zip.ZipFile;

public final class StartupVolumeGuardTest {
    interface Action {
        void run() throws Exception;
    }

    static int negatives;

    static void check(boolean b) {
        if (!b) {
            throw new AssertionError("startup volume contract");
        }
    }

    static void reject(Action a) throws Exception {
        try {
            a.run();
        } catch (Failure expected) {
            negatives++;

            return;
        }

        throw new AssertionError("unsafe startup input accepted");
    }

    // Execute the actual emitted constructor block against retained default/speaker/BT settings.
    // Reload uses unchanged stored values: startup writes0; later public UP writes7 and survives
    // reload.
    static void simulateStartup(List<Instruction> code, int begin, int registers, boolean stream) {
        var db = new TreeMap<Integer, Integer>(Map.of(0x40000000, 9, 2, 6, 128, 11));
        var map = new TreeMap<Integer, Integer>(db);
        Object[] r = new Object[registers];
        int[] addresses = new int[code.size() + 1];

        for (int i = 0; i < code.size(); i++) {
            addresses[i + 1] = addresses[i] + code.get(i).getCodeUnits();
        }

        int at = begin, steps = 0;
        Set<Integer> persisted = new HashSet<>();

        while (at < begin + 15) {
            check(++steps < 100);
            Instruction ins = code.get(at);
            int next = at + 1;

            switch (ins.getOpcode()) {
                case IGET_OBJECT -> r[((OneRegisterInstruction) ins).getRegisterA()] = map;
                case CONST_4, CONST ->
                        r[((OneRegisterInstruction) ins).getRegisterA()] =
                                ((NarrowLiteralInstruction) ins).getNarrowLiteral();
                case MOVE_RESULT ->
                        r[((OneRegisterInstruction) ins).getRegisterA()] = r[registers - 1];
                case IF_GE -> {
                    var i = (TwoRegisterInstruction) ins;

                    if ((int) r[i.getRegisterA()] >= (int) r[i.getRegisterB()]) {
                        next =
                                Arrays.binarySearch(
                                        addresses,
                                        addresses[at] + ((OffsetInstruction) ins).getCodeOffset());
                    }
                }
                case GOTO ->
                        next =
                                Arrays.binarySearch(
                                        addresses,
                                        addresses[at] + ((OffsetInstruction) ins).getCodeOffset());
                case ADD_INT_LIT8 -> {
                    var i = (TwoRegisterInstruction) ins;
                    r[i.getRegisterA()] =
                            (int) r[i.getRegisterB()]
                                    + ((NarrowLiteralInstruction) ins).getNarrowLiteral();
                }
                case INVOKE_VIRTUAL, INVOKE_DIRECT -> {
                    var i = (FiveRegisterInstruction) ins;
                    String ref = ((ReferenceInstruction) ins).getReference().toString();
                    int b = i.getRegisterD(), c = i.getRegisterE();

                    if (ref.endsWith("->size()I")) {
                        r[registers - 1] = map.size();
                    } else if (ref.endsWith("->keyAt(I)I")) {
                        r[registers - 1] = new ArrayList<>(map.keySet()).get((int) r[b]);
                    } else if (ref.endsWith("->setValueAt(II)V")) {
                        map.put(new ArrayList<>(map.keySet()).get((int) r[b]), (int) r[c]);
                    } else if (ref.endsWith("->put(II)V")) {
                        map.put((int) r[b], (int) r[c]);
                    } else {
                        check(
                                ref.contains(
                                        stream
                                                ? StartupVolumeGuard.PERSIST
                                                : "persistVolumeGroup"));
                        db.put((int) r[b], map.get((int) r[b]));
                        persisted.add((int) r[b]);
                    }
                }
                default -> throw new AssertionError(ins.getOpcode());
            }

            at = next;
        }

        check(
                map.size() == 3
                        && map.values().stream().allMatch(v -> v == 0)
                        && persisted.equals(db.keySet())
                        && db.values().stream().allMatch(v -> v == 0));
        var reloaded = new TreeMap<>(db);
        check(reloaded.values().stream().allMatch(v -> v == 0));
        db.put(2, 7);
        reloaded = new TreeMap<>(db);
        check(reloaded.get(2) == 7); // no constructor rerun on ordinary settings reload
    }

    /**
     * Constants of the step of the profile directory that reaches the startup-volume guard: its own
     * step, otherwise the clean secondary navigation step; run from the repository root.
     */
    static Map<String, Object> constants(String directory) throws Exception {
        Path patches = Path.of("profiles", directory, "patches");
        Path step = patches.resolve(StartupVolumeGuard.NAME + ".json");

        if (!Files.isRegularFile(step)) {
            step = patches.resolve(SecondaryNavigationGuard.NAME + ".json");
        }

        return Json.obj(Json.object(Json.parse(Files.readString(step))), "constants");
    }

    // Arguments: PROFILE_DIRECTORY SERVICES_JAR STAGE.
    public static void main(String[] args) throws Exception {
        Map<String, Object> step = constants(args[0]);
        var foreign = new java.util.HashMap<String, Object>(step);

        foreign.put(StepConstants.PINS, List.of("0".repeat(64)));
        Path original = Path.of(args[1]), stage = Path.of(args[2]);

        Files.createDirectories(stage);
        byte[] dex;

        try (var z = new ZipFile(original.toFile())) {
            dex = z.getInputStream(z.getEntry("classes.dex")).readAllBytes();
        }

        StartupVolumeGuard.validate(step, 30, original, dex);
        byte[] after = StartupVolumeGuard.apply(step, 30, original, dex);
        check(Arrays.equals(after, StartupVolumeGuard.apply(step, 30, original, dex)));
        check(
                NativeCore.hash(after)
                        .equals(
                                "108491886c2ed3e8bdbd72849d6d5469796b9b1e5421f3f95e85a999505e2f00"));
        Files.write(stage.resolve("startup-volumes.dex"), after);
        var before = PassionServiceForegroundGuard.classes(dex);
        var actual = PassionServiceForegroundGuard.classes(after);
        int changed = 0, zeroMin = 0, resetSites = 0, defaults = 0;

        for (var c : actual.values()) {
            for (var m : c.getMethods()) {
                if (StartupVolumeGuard.target(m)) {
                    changed++;

                    if (c.getType().equals(StartupVolumeGuard.AUDIO)
                            && m.getName().equals("<clinit>")) {
                        for (var i : m.getImplementation().getInstructions()) {
                            if (i instanceof ArrayPayload a
                                    && a.getArrayElements().size() == 12
                                    && a.getArrayElements().stream()
                                            .allMatch(n -> n.intValue() == 0)) {
                                zeroMin++;
                            }
                        }
                    } else if (c.getType().equals(StartupVolumeGuard.AUDIO)) {
                        for (var i : m.getImplementation().getInstructions()) {
                            if (i instanceof ReferenceInstruction r
                                    && r.getReference()
                                            .toString()
                                            .equals("Ljava/util/Arrays;->fill([II)V")) {
                                defaults++;
                            }
                        }
                    } else {
                        resetSites += checkResetSites(c, m);
                    }
                }
            }
        }

        check(changed == 4 && zeroMin == 1 && resetSites == 2 && defaults == 1);
        var inverse = StartupVolumeGuard.transform(actual, true);
        check(inverse.keySet().equals(before.keySet()));

        for (var key : before.keySet()) {
            check(
                    FrameworkDexMaterializer.semantic(before.get(key))
                            .equals(FrameworkDexMaterializer.semantic(inverse.get(key))));
        }

        checkOriginalMinimums(before);
        // All later set/adjust/get-valid-index methods and maximum-volume data stay stock.
        reject(() -> StartupVolumeGuard.apply(foreign, 30, original, dex));
        reject(() -> StartupVolumeGuard.apply(step, 29, original, dex));
        reject(() -> StartupVolumeGuard.apply(step, 30, original, after));
        Path wrong = stage.resolve("wrong.jar");
        Files.write(wrong, new byte[] {1});
        reject(() -> StartupVolumeGuard.apply(step, 30, wrong, dex));
        var missing = new TreeMap<String, ClassDef>(before);
        missing.remove(StartupVolumeGuard.STREAM);
        reject(() -> StartupVolumeGuard.transform(missing, false));
        reject(() -> StartupVolumeGuard.transform(actual, false));
        Files.write(stage.resolve("startup-volumes.dex"), after);
        var receipt =
                Map.ofEntries(
                        Map.entry("changed_initialization_methods", 4),
                        Map.entry("stream_count", 12),
                        Map.entry("all_stream_minimums_zero", true),
                        Map.entry("stream_and_group_indices_and_db_reset", true),
                        Map.entry("native_defaults_zero", true),
                        Map.entry("startup_reload_then_UP_reload_simulated", true),
                        Map.entry("inverse_all_classes_exact", true),
                        Map.entry("later_volume_controls_unchanged", true),
                        Map.entry("negative_checks", negatives),
                        Map.entry("dex_sha256", NativeCore.hash(after)),
                        Map.entry("guest_started", false));
        Files.writeString(stage.resolve("receipt.json"), Json.write(receipt) + "\n");
        System.out.println(Json.write(receipt));
    }

    static int checkResetSites(ClassDef c, Method m) {
        int resetSites = 0;
        var ins = new ArrayList<org.jf.dexlib2.iface.instruction.Instruction>();

        m.getImplementation().getInstructions().forEach(ins::add);

        for (int i = 0; i < ins.size(); i++) {
            if (ins.get(i) instanceof ReferenceInstruction r
                    && r.getReference().toString().equals(c.getType() + "->readSettings()V")) {
                check(
                        ((ReferenceInstruction) ins.get(i + 1))
                                .getReference()
                                .toString()
                                .endsWith("->mIndexMap:Landroid/util/SparseIntArray;"));
                check(
                        ((ReferenceInstruction) ins.get(i + 3))
                                .getReference()
                                .toString()
                                .endsWith("->size()I"));
                check(ins.get(i + 5).getOpcode() == org.jf.dexlib2.Opcode.IF_GE);
                check(
                        ((ReferenceInstruction) ins.get(i + 9))
                                .getReference()
                                .toString()
                                .endsWith("->setValueAt(II)V"));
                check(
                        ((ReferenceInstruction) ins.get(i + 10))
                                .getReference()
                                .toString()
                                .contains(
                                        c.getType().equals(StartupVolumeGuard.STREAM)
                                                ? StartupVolumeGuard.PERSIST
                                                : "persistVolumeGroup"));
                simulateStartup(
                        ins,
                        i + 1,
                        m.getImplementation().getRegisterCount(),
                        c.getType().equals(StartupVolumeGuard.STREAM));
                resetSites++;
            }
        }

        return resetSites;
    }

    static void checkOriginalMinimums(Map<String, ClassDef> before) {
        int originalMin = 0;

        for (var c : before.values()) {
            for (var m : c.getMethods()) {
                if (c.getType().equals(StartupVolumeGuard.AUDIO)
                        && m.getName().equals("<clinit>")) {
                    for (var i : m.getImplementation().getInstructions()) {
                        if (i instanceof ArrayPayload a
                                && a.getElementWidth() == 4
                                && a.getArrayElements().stream()
                                        .map(Number::intValue)
                                        .toList()
                                        .equals(List.of(1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0))) {
                            originalMin++;
                        }
                    }
                }
            }
        }

        check(originalMin == 1);
    }
}
