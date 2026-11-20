package com.voyemu.prepare;

import java.util.HashSet;

public final class NxpMediaLibraryTest {
    public static void main(String[] args) {
        if (!NxpMediaLibrary.selected(
                28, NxpMediaLibrary.PIN, "VoyemuNxpMediaCallbacks.jar", NxpMediaLibrary.TYPES)) {
            throw new AssertionError("exact owner rejected");
        }

        var missing = new HashSet<>(NxpMediaLibrary.TYPES);
        missing.remove(missing.iterator().next());
        var extra = new HashSet<>(NxpMediaLibrary.TYPES);
        extra.add("Lunrelated/Class;");

        if (NxpMediaLibrary.selected(
                        29,
                        NxpMediaLibrary.PIN,
                        "VoyemuNxpMediaCallbacks.jar",
                        NxpMediaLibrary.TYPES)
                || NxpMediaLibrary.selected(
                        28, "0".repeat(64), "VoyemuNxpMediaCallbacks.jar", NxpMediaLibrary.TYPES)
                || NxpMediaLibrary.selected(
                        28, NxpMediaLibrary.PIN, "other.jar", NxpMediaLibrary.TYPES)
                || NxpMediaLibrary.selected(
                        28, NxpMediaLibrary.PIN, "VoyemuNxpMediaCallbacks.jar", missing)
                || NxpMediaLibrary.selected(
                        28, NxpMediaLibrary.PIN, "VoyemuNxpMediaCallbacks.jar", extra)) {
            throw new AssertionError("owner/API/pin/output/exactclosure guard bypass");
        }

        System.out.println("NXP exact owner donor and six scope rejection cases passed");
    }
}
