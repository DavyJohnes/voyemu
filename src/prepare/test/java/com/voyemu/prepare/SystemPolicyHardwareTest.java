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
    static final List<String> PROFILES =
            List.of(
                    "free-09-chinese-20260106-6.0.3",
                    "free-11-chinese-20250710-6.11.1",
                    "free-11-chinese-20260731-7.3.1");
    static final String STUB = "Lcom/qinggan/canbus/ICanBusService$Stub;";

    static void need(boolean x, String message) {
        if (!x) {
            throw new AssertionError(message);
        }
    }

    static int api(String profile) {
        return profile.startsWith("free-09-") ? 28 : 30;
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

    static void reject(String profile, int api, String pin, DexClasses classes, String message) {
        var before = lines(classes);
        boolean rejected = false;

        try {
            SystemPolicyHardwareGuard.apply(profile, api, pin, classes);
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

    static void check(String profile, Path policy, Path donor) throws Exception {
        int api = api(profile);
        String pin = SystemPolicyHardwareGuard.PINS.get(profile);

        NativeCore.pinned(policy, pin);
        var classes = classes(policy, api);
        var original = lines(classes);
        reject("free-09-chinese-20221230-nxp-rc45", api, pin, classes, "NXP profile accepted");
        reject(profile, api == 28 ? 30 : 28, pin, classes, "Wrong API accepted");
        reject(profile, api, "0".repeat(64), classes, "Wrong APK accepted");
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
        reject(profile, api, pin, classes, "Changed OEM code accepted");
        classes = classes(policy, api);
        SystemPolicyHardwareGuard.apply(profile, api, pin, classes);
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
            List<String> before = original.get(entry.getKey());
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
                    profile + ": changes beyond the hardware guards in " + entry.getKey());
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
        need(guard >= 0 && guard < constructor, profile + ": no adapter must skip BluetoothPolicy");
        var charging =
                lines(classes).get(SystemPolicyHardwareGuard.SERVICE + "->startChargingService()V");
        int early = first(charging, "return-void");
        need(
                early >= 0 && early < first(charging, "new-instance v0, Landroid/content/Intent;"),
                profile + ": no adapter must return before the charging service intent");
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
                profile + ": guarded code must survive DEX serialization");
        var codes = transactions(policy, api);
        need(
                codes.size() > 50 && codes.equals(transactions(donor, api)),
                profile + ": SystemPolicy CanBus transactions differ from the qg.canbus donor");
    }

    public static void main(String[] args) throws Exception {
        need(args.length == 6, "SystemPolicy and qg.canbus donor APK of each Free profile");

        for (int i = 0; i < PROFILES.size(); i++) {
            check(PROFILES.get(i), Path.of(args[2 * i]), Path.of(args[2 * i + 1]));
        }

        System.out.println(
                "Free SystemPolicy hardware PASS: two exact adapter guards on 6.0.3, 6.11.1 and"
                    + " 7.3.1, API30 car service guard, all other code preserved, serialized DEX,"
                    + " wrong profile/API/APK/code rejected, CanBus transactions equal the"
                    + " qg.canbus donor");
    }
}
