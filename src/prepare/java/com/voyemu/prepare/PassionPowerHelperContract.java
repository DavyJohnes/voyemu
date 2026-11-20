package com.voyemu.prepare;

import org.jf.dexlib2.AccessFlags;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;

import java.nio.file.Path;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/** Structural admission of repository-built helpers, bound to the ordinary artifact result. */
final class PassionPowerHelperContract {
    static final String CONTRACT = "passion-display-power-helper-v1";
    static final String SDK = "patched:passion-api30-secondary-navigation";
    static final String OEM = "patched:MegaNexusService";
    private static final String PREFIX = "Lcom/voyemu/display/";

    private PassionPowerHelperContract() {}

    static boolean requested(Map<String, Object> patches) {
        boolean sdk =
                declares(
                        patches,
                        "passion-api30-secondary-navigation",
                        "passion-api30-display-power.json");
        boolean oem = declares(patches, "MegaNexusService", "passion-api30-nexus-power.json");

        Failure.require(sdk == oem, "PASSION_POWER_HELPER", "Partial power graph rejected", 5);

        return sdk;
    }

    private static boolean declares(Map<String, Object> patches, String artifact, String step) {
        return patches.containsKey(artifact)
                && Json.list(Json.obj(patches, artifact).get("steps")).contains(step);
    }

    static String slot(String artifact) {
        return artifact.equals(SDK) ? "classes3.dex" : "classes2.dex";
    }

    static void verify(String artifact, byte[] dex) throws Failure {
        Set<String> required =
                artifact.equals(SDK)
                        ? Set.of(
                                "DmsPowerCommand;->exec(Ljava/lang/Object;Ljava/lang/Object;Ljava/io/FileDescriptor;Ljava/io/FileDescriptor;Ljava/io/FileDescriptor;[Ljava/lang/String;Ljava/lang/Object;Ljava/lang/Object;)I",
                                "DmsDisplayPowerBackend;->effectiveState(Ljava/lang/Object;I)I")
                        : Set.of(
                                "OemDisplayPower;->main([Ljava/lang/String;)V",
                                "OemDisplayPower;->before(Ljava/lang/Object;)V",
                                "OemDisplayPower;->bootstrap(Ljava/lang/Object;)V",
                                "OemDisplayPower;->send(Ljava/lang/Object;Z)Z",
                                "OemDisplayPower;->result(Ljava/lang/Object;Ljava/lang/Object;I)V",
                                "OemDisplayPower;->finish(Ljava/lang/Object;)V");

        Failure.require(
                (artifact.equals(SDK) || artifact.equals(OEM)) && dex != null,
                "PASSION_POWER_HELPER",
                "Declared power helper DEX required",
                5);
        Set<String> found = new HashSet<>(), classes = new HashSet<>();

        for (var type : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            String name = type.getType();
            Failure.require(
                    name.startsWith(PREFIX) && classes.add(name),
                    "PASSION_POWER_HELPER",
                    "Unique private power helper classes required",
                    5);

            for (var method : type.getMethods()) {
                String descriptor =
                        name.substring(PREFIX.length())
                                + "->"
                                + method.getName()
                                + "("
                                + String.join("", method.getParameterTypes())
                                + ")"
                                + method.getReturnType();

                if (required.contains(descriptor)) {
                    Failure.require(
                            method.getImplementation() != null
                                    && AccessFlags.PUBLIC.isSet(method.getAccessFlags())
                                    && AccessFlags.STATIC.isSet(method.getAccessFlags())
                                    && found.add(descriptor),
                            "PASSION_POWER_HELPER",
                            "Executable public static power hook required",
                            5);
                }
            }
        }

        Failure.require(
                found.equals(required),
                "PASSION_POWER_HELPER",
                "Complete power helper hook ABI required",
                5);
    }

    static Map<String, Object> receipt(String artifact, Path archive) throws Exception {
        byte[] dex = NativeCore.entries(archive).get(slot(artifact));

        verify(artifact, dex);

        return Map.of(
                "contract",
                CONTRACT,
                "artifact",
                artifact,
                "archive_sha256",
                FilesSupport.hash(archive),
                "entry",
                slot(artifact),
                "sha256",
                NativeCore.hash(dex),
                "bytes",
                (long) dex.length);
    }
}
