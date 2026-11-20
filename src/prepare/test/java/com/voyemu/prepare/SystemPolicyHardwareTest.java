package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/**
 * The OEM SystemPolicy of each Free profile gets the two Bluetooth adapter guards and API30 car
 * service guard; its cluster data calls use the CanBus Binder transactions of the own qg.canbus.
 */
public final class SystemPolicyHardwareTest {
    /** The pinned SystemPolicy original of one Free firmware and the API of its guest. */
    record Original(String pin, int api) {}

    static final List<Original> ORIGINALS =
            List.of(
                    new Original(
                            "6c223315e64d14f9f1c715c42474257df772ce218e40094cf91b1cb012b2fadc", 28),
                    new Original(
                            "02749c34c515b8dd2ecdb6f5030762c292bc47fc3b79214d7965cb5cf56226eb", 30),
                    new Original(
                            "a81710fcbb382d9a1c449b240c2dc40aebc3d2e3cc309339ec1be3c5fbb2de00",
                            30));
    static final String STUB = "Lcom/qinggan/canbus/ICanBusService$Stub;";

    static void need(boolean x, String message) {
        if (!x) {
            throw new AssertionError(message);
        }
    }

    static DexClasses classes(Path apk, int api) throws Exception {
        Map<String, byte[]> archive = new TreeMap<>(NativeCore.entries(apk));

        return new DexClasses(
                archive,
                archive.keySet().stream()
                        .filter(n -> n.matches("classes[0-9]*\\.dex"))
                        .sorted()
                        .toList(),
                api);
    }

    /** Instructions of every method of the guarded classes, without branch target addresses. */
    static Map<String, List<String>> lines(DexClasses classes) {
        Map<String, List<String>> result = new TreeMap<>();

        for (String type :
                List.of(SystemPolicyHardwareGuard.MANAGER, SystemPolicyHardwareGuard.SERVICE)) {
            for (var method : classes.get(type).getMethods()) {
                var code = method.getImplementation();

                if (code != null) {
                    result.put(
                            DexEdits.descriptor(method),
                            DexEdits.render(
                                            code,
                                            new DexText.Frame(
                                                    code.getRegisterCount(),
                                                    DexEdits.parameters(method)))
                                    .stream()
                                    .map(l -> l.replaceAll(":[a-z]+_[0-9a-f]+$", ""))
                                    .toList());
                }
            }
        }

        return result;
    }

    static int first(List<String> lines, String prefix) {
        for (int i = 0; i < lines.size(); i++) {
            if (lines.get(i).startsWith(prefix)) {
                return i;
            }
        }

        return -1;
    }

    static void reject(
            Map<String, Object> step, int api, String pin, DexClasses classes, String message) {
        var before = lines(classes);
        boolean rejected = false;

        try {
            SystemPolicyHardwareGuard.apply(step, api, pin, classes);
        } catch (Failure expected) {
            rejected = true;
        }

        need(rejected && lines(classes).equals(before), message);
    }

    /** TRANSACTION_* codes of the CanBus Binder stub bundled in an APK. */
    static Map<String, Integer> transactions(Path apk, int api) throws Exception {
        Map<String, Integer> codes = new TreeMap<>();

        for (var field : classes(apk, api).get(STUB).getStaticFields()) {
            if (field.getName().startsWith("TRANSACTION_")) {
                codes.put(
                        field.getName(),
                        ((org.jf.dexlib2.iface.value.IntEncodedValue) field.getInitialValue())
                                .getValue());
            }
        }

        return codes;
    }

    static void check(Original original, Path policy, Path donor) throws Exception {
        int api = original.api();
        String pin = original.pin(), label = pin.substring(0, 12);
        Map<String, Object> step = Map.of(StepConstants.PINS, List.of(pin));

        NativeCore.pinned(policy, pin);
        var classes = classes(policy, api);
        var baseline = lines(classes);
        reject(step, 29, pin, classes, "Wrong API accepted");
        reject(step, api, "0".repeat(64), classes, "Wrong APK accepted");
        // Changed OEM code: the anchor of the second guard no longer matches.
        String build = "sget-object v0, Landroid/os/Build;->";
        classes.edit(
                List.of(
                        new DexEdits.MethodEdit(
                                SystemPolicyHardwareGuard.MANAGER
                                        + "-><init>(Landroid/content/Context;)V",
                                null,
                                List.of(
                                        DexEdits.Edit.replace(
                                                List.of(build + "PRODUCT:Ljava/lang/String;"),
                                                List.of(build + "MODEL:Ljava/lang/String;"))))));
        reject(step, api, pin, classes, "Changed OEM code accepted");
        classes = classes(policy, api);
        SystemPolicyHardwareGuard.apply(step, api, pin, classes);
        Map<String, Integer> inserted = new TreeMap<>();

        for (var edit : SystemPolicyHardwareGuard.EDITS) {
            for (var change : edit.edits()) {
                inserted.merge(
                        edit.method(),
                        (int) change.insert().stream().filter(l -> !DexText.isLabel(l)).count(),
                        Integer::sum);
            }
        }

        need(inserted.size() == 2, "Two guarded methods expected");

        if (api == 30) {
            inserted.merge(
                    SystemPolicyHardwareGuard.SERVICE + "->startChargingService()V",
                    4,
                    Integer::sum);
        }

        for (var entry : lines(classes).entrySet()) {
            List<String> before = baseline.get(entry.getKey());
            int matched = 0;

            for (String line : entry.getValue()) {
                if (matched < before.size() && before.get(matched).equals(line)) {
                    matched++;
                }
            }

            need(
                    matched == before.size()
                            && entry.getValue().size() - before.size()
                                    == inserted.getOrDefault(entry.getKey(), 0),
                    label + ": changes beyond the hardware guards in " + entry.getKey());
        }

        var manager =
                lines(classes)
                        .get(
                                SystemPolicyHardwareGuard.MANAGER
                                        + "-><init>(Landroid/content/Context;)V");
        int guard = first(manager, "if-eqz v0, "),
                constructor =
                        first(
                                manager,
                                "new-instance v1,"
                                        + " Lcom/qinggan/systempolicy/bluetooth/BluetoothPolicy;");
        need(guard >= 0 && guard < constructor, label + ": no adapter must skip BluetoothPolicy");
        var charging =
                lines(classes).get(SystemPolicyHardwareGuard.SERVICE + "->startChargingService()V");
        int early = first(charging, "return-void");
        need(
                early >= 0 && early < first(charging, "new-instance v0, Landroid/content/Intent;"),
                label + ": no adapter must return before the charging service intent");
        Map<String, byte[]> archive = new TreeMap<>(NativeCore.entries(policy));
        classes.write(archive);
        need(
                lines(
                                new DexClasses(
                                        archive,
                                        archive.keySet().stream()
                                                .filter(n -> n.matches("classes[0-9]*\\.dex"))
                                                .sorted()
                                                .toList(),
                                        api))
                        .equals(lines(classes)),
                label + ": guarded code must survive DEX serialization");
        var codes = transactions(policy, api);
        need(
                codes.size() > 50 && codes.equals(transactions(donor, api)),
                label + ": SystemPolicy CanBus transactions differ from the qg.canbus donor");
    }

    public static void main(String[] args) throws Exception {
        need(args.length == 6, "SystemPolicy and qg.canbus donor APK of each Free profile");

        for (int i = 0; i < ORIGINALS.size(); i++) {
            check(ORIGINALS.get(i), Path.of(args[2 * i]), Path.of(args[2 * i + 1]));
        }

        System.out.println(
                "Free SystemPolicy hardware PASS: two exact adapter guards on each pinned original,"
                        + " API30 car service guard, all other code preserved, serialized DEX,"
                        + " wrong API/APK/code rejected, CanBus transactions equal the"
                        + " qg.canbus donor");
    }
}
