package com.voyemu.prepare;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

/**
 * Every selected OEM APK carrying the original NVRAM native API needs the existing runtime shim.
 */
public final class NxpApkRuntimeCoverageTest {
    static final String ID = "free-09-chinese-20221230-nxp-rc45",
            RUNTIME = "component:qg-runtime",
            STEP = "add-qg-runtime.json";

    static List<String> missing(Map<String, Object> data, Set<String> needs) {
        var result = new ArrayList<String>();
        var patches = Json.obj(data, "patches");

        for (String name : needs) {
            var patch = Json.obj(patches, name);

            if (!Json.list(patch.getOrDefault("uses", List.of())).contains(RUNTIME)
                    || !Json.list(patch.get("steps")).contains(STEP)) {
                result.add(name);
            }
        }

        return result;
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]);
        var data =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve("profiles/" + ID + "/profile.json"))));
        var needs = new TreeSet<String>();
        var inventory = new ArrayList<Map<String, Object>>();

        for (Object item : Json.list(data.get("install"))) {
            var install = Json.object(item);

            if (!"apk".equals(install.get("type"))) {
                continue;
            }

            String artifact = Json.str(install, "artifact");

            if (!artifact.startsWith("patched:")) {
                inventory.add(
                        Map.of(
                                "artifact",
                                artifact,
                                "kind",
                                "owned-component",
                                "oem_nvram",
                                false));
                continue;
            }

            String name = artifact.substring(8);
            var patch = Json.obj(Json.obj(data, "patches"), name);
            String input = Json.str(patch, "input");

            if (!input.startsWith("original:")) {
                throw new AssertionError("Explicit original APK required " + name);
            }

            var original = Json.obj(Json.obj(data, "original"), input.substring(9));
            Path file =
                    root.resolve(
                            ".local/profiles/" + ID + "/originals/" + Json.str(original, "file"));
            NativeCore.pinned(file, Json.str(original, "sha256"));
            boolean nvram = false;
            var container = DexFileFactory.loadDexContainer(file.toFile(), Opcodes.forApi(28));

            for (String entry : container.getDexEntryNames()) {
                for (var type : container.getEntry(entry).getDexFile().getClasses()) {
                    if (type.getType().equals("Lcom/qinggan/os/NvramNative;")) {
                        nvram = true;
                    }
                }
            }

            if (nvram) {
                needs.add(name);
            }

            inventory.add(
                    Map.of(
                            "artifact",
                            artifact,
                            "original_sha256",
                            Json.str(original, "sha256"),
                            "oem_nvram",
                            nvram));
        }

        System.out.println(
                Json.write(
                        Map.of(
                                "schema",
                                1,
                                "profile",
                                ID,
                                "selected_apk_inventory",
                                inventory,
                                "native_runtime_required",
                                needs,
                                "missing",
                                missing(data, needs))));

        if (!missing(data, needs).isEmpty()) {
            throw new AssertionError(
                    "Selected OEM APKs missing existing qg-runtime dependency "
                            + missing(data, needs));
        }

        for (String name : needs) {
            var copy = Json.object(Json.parse(Json.write(data)));
            var patches = Json.obj(copy, "patches");
            var patch = Json.obj(patches, name);
            patch.put("uses", List.of());
            patches.put(name, patch);
            copy.put("patches", patches);

            if (!missing(copy, needs).contains(name)) {
                throw new AssertionError("Missing runtime use was accepted " + name);
            }
        }

        System.out.println(
                "NXP APK runtime coverage PASS selectedAPK="
                        + inventory.size()
                        + ", nativeNvramOwners="
                        + needs.size()
                        + ", missing-dependency negative checks="
                        + needs.size());
    }
}
