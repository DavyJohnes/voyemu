package com.voyemu.prepare;

import java.util.HashSet;
import java.util.Set;

/**
 * Exact owner scope and inverse rejection for the finite Setting donor JARs; no OEM bytes in
 * source.
 */
public final class NxpSettingsLibraryTest {
    public static void main(String[] args) {
        String pin = NxpSettingsLibrary.PIN, profile = TestProfiles.SYNTHETIC;

        checkExactDonors(profile, pin);
        Set<String> all = new HashSet<>(NxpSettingsLibrary.RUNTIME_TYPES);
        all.addAll(NxpSettingsLibrary.MANAGER_TYPES);

        if (all.size() != 75) {
            throw new AssertionError("donor families overlap");
        }

        var full = systemUiClosures(all, profile, pin);

        if (full.size() != 226 || !full.contains("Lcom/qinggan/bluetooth/profile/IBTCallback;")) {
            throw new AssertionError("SystemUI complete closure differs");
        }

        if (all.contains("Lcom/qinggan/util/Log;")) {
            throw new AssertionError("Log must stay owned by the media-callbacks donor");
        }

        if (NxpSettingsLibrary.RUNTIME_TYPES.contains("Lcom/qinggan/util/Log;")) {
            throw new AssertionError("Log must ride its authoritative classes2.dex donor");
        }

        checkDescriptors();
        // Negative scope cases: owner, API, pin, output, and exact closure must all match.
        boolean bypassed = scopeBypassed(profile, pin);

        if (bypassed) {
            throw new AssertionError(
                    "owner/API/pin/output/exact-closure settings donor guard bypass");
        }

        System.out.println(
                "NXP exact Setting donor scopes plus 12 owner/API/pin/output/swap/media-cross"
                        + " rejection cases passed");
    }

    static void checkExactDonors(String profile, String pin) {
        if (!NxpSettingsLibrary.selected(
                28, pin, "VoyemuNxpSettingsRuntime.jar", NxpSettingsLibrary.RUNTIME_TYPES)) {
            throw new AssertionError("exact runtime donor rejected");
        }

        if (!NxpSettingsLibrary.selected(
                28, pin, "VoyemuNxpSettingsManagers.jar", NxpSettingsLibrary.MANAGER_TYPES)) {
            throw new AssertionError("exact managers donor rejected");
        }

        // Positive structural invariants of the finite closure contract.
        if (NxpSettingsLibrary.RUNTIME_TYPES.size() != 25
                || NxpSettingsLibrary.MANAGER_TYPES.size() != 50) {
            throw new AssertionError("donor type count drifted");
        }
    }

    static void checkDescriptors() {
        for (String type : NxpSettingsLibrary.RUNTIME_TYPES) {
            if (!type.startsWith("L") || !type.endsWith(";") || type.contains("..")) {
                throw new AssertionError("malformed descriptor");
            }
        }
    }

    static Set<String> systemUiClosures(Set<String> all, String profile, String pin) {
        var full = new HashSet<>(all);
        var groups =
                java.util.List.of(
                        NxpSettingsLibrary.SYSTEM_UI_RUNTIME_A,
                        NxpSettingsLibrary.SYSTEM_UI_RUNTIME_B,
                        NxpSettingsLibrary.SYSTEM_UI_RUNTIME_C);
        int[] sizes = {64, 63, 24};
        String[] names = {"A", "B", "C"};

        for (int i = 0; i < groups.size(); i++) {
            var types = groups.get(i);

            if (types.size() != sizes[i]) {
                throw new AssertionError("SystemUI closure count differs");
            }

            for (String type : types) {
                if (!full.add(type)) {
                    throw new AssertionError("SystemUI donor closure overlap");
                }
            }

            String output = "VoyemuNxpSystemUiRuntime" + names[i] + ".jar",
                    reference =
                            "component:systemui-runtime-"
                                    + names[i].toLowerCase(java.util.Locale.ROOT);

            if (!NxpSettingsLibrary.selected(28, pin, output, types)
                    || !NxpSettingsLibrary.slot(profile, 28, reference, types)) {
                throw new AssertionError("exact SystemUI closure rejected");
            }

            var omitted = new HashSet<>(types);
            omitted.remove(omitted.iterator().next());

            if (NxpSettingsLibrary.selected(28, pin, output, omitted)
                    || NxpSettingsLibrary.slot(profile, 28, reference, omitted)
                    || NxpSettingsLibrary.selected(30, pin, output, types)
                    || NxpSettingsLibrary.selected(28, "0".repeat(64), output, types)) {
                throw new AssertionError("SystemUI finite closure scope bypass");
            }
        }

        return full;
    }

    static boolean scopeBypassed(String profile, String pin) {
        var missing = new HashSet<>(NxpSettingsLibrary.RUNTIME_TYPES);

        missing.remove(missing.iterator().next());
        var extra = new HashSet<>(NxpSettingsLibrary.MANAGER_TYPES);
        extra.add("Lunrelated/Class;");
        var swappedOut = new HashSet<>(NxpSettingsLibrary.RUNTIME_TYPES);
        boolean bypassed = false;
        bypassed |=
                NxpSettingsLibrary.selected(
                        29, pin, "VoyemuNxpSettingsRuntime.jar", NxpSettingsLibrary.RUNTIME_TYPES);
        bypassed |=
                NxpSettingsLibrary.selected(
                        28,
                        "0".repeat(64),
                        "VoyemuNxpSettingsRuntime.jar",
                        NxpSettingsLibrary.RUNTIME_TYPES);
        bypassed |=
                NxpSettingsLibrary.selected(28, pin, "other.jar", NxpSettingsLibrary.RUNTIME_TYPES);
        bypassed |= NxpSettingsLibrary.selected(28, pin, "VoyemuNxpSettingsRuntime.jar", missing);
        bypassed |= NxpSettingsLibrary.selected(28, pin, "VoyemuNxpSettingsManagers.jar", extra);
        // Inverse: runtime types must never be accepted under the managers output name and vice
        // versa.
        bypassed |=
                NxpSettingsLibrary.selected(28, pin, "VoyemuNxpSettingsManagers.jar", swappedOut);
        bypassed |=
                NxpSettingsLibrary.selected(
                        28, pin, "VoyemuNxpSettingsRuntime.jar", NxpSettingsLibrary.MANAGER_TYPES);
        // The media donor scope must never select a settings donor slot.
        bypassed |=
                NxpSettingsLibrary.selected(
                        28,
                        NxpMediaLibrary.PIN,
                        "VoyemuNxpSettingsRuntime.jar",
                        NxpMediaLibrary.TYPES);
        bypassed |=
                NxpMediaLibrary.selected(
                        28, pin, "VoyemuNxpSettingsRuntime.jar", NxpSettingsLibrary.RUNTIME_TYPES);
        var logRiding = new HashSet<>(NxpSettingsLibrary.MANAGER_TYPES);
        logRiding.add("Lcom/qinggan/util/Log;");
        bypassed |=
                NxpSettingsLibrary.selected(28, pin, "VoyemuNxpSettingsManagers.jar", logRiding);
        bypassed |=
                NxpMediaLibrary.selected(
                        28, pin, "VoyemuNxpMediaCallbacks.jar", NxpSettingsLibrary.RUNTIME_TYPES);

        return bypassed;
    }
}
