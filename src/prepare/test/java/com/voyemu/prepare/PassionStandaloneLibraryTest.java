package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.concurrent.TimeUnit;

public final class PassionStandaloneLibraryTest {
    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static String verify(Path tool, Path file, boolean valid) throws Exception {
        Process process =
                new ProcessBuilder(tool.toString(), "-c", file.toString())
                        .redirectErrorStream(true)
                        .start();

        if (!process.waitFor(5, TimeUnit.SECONDS)) {
            process.destroyForcibly();
            process.waitFor();
            throw new AssertionError("bounded actual ART verifier timed out");
        }

        byte[] bytes = process.getInputStream().readNBytes(65537);
        check(bytes.length <= 65536, "bounded verifier output");
        String output = new String(bytes, java.nio.charset.StandardCharsets.UTF_8);
        check(
                valid ? process.exitValue() == 0 : process.exitValue() != 0,
                "actual verifier expected " + (valid ? "GREEN" : "RED") + ": " + output);

        if (!valid) {
            check(
                    output.contains("Hiddenapi") || output.contains("section of type 61440"),
                    "RED must be actual hiddenapi format error");
        }

        return output;
    }

    public static void main(String[] args) throws Exception {
        check(args.length == 3, "ROOT OLD_RESULT FRESH_TEST_STAGE");
        Path root = Path.of(args[0]).toRealPath(),
                result = Path.of(args[1]).toRealPath(),
                stage = Path.of(args[2]);
        check(!Files.exists(stage), "fresh exact test stage");
        Files.createDirectories(stage);
        Path tool = root.resolve(".local/toolchains/native-sdk/build-tools/36.1.0/dexdump"),
                owner =
                        root.resolve(
                                ".local/profiles/"
                                        + TestProfiles.pinning(root, PassionStandaloneLibrary.PIN)
                                        + "/originals/framework/framework.jar");
        check(FilesSupport.hash(owner).equals(PassionStandaloneLibrary.PIN), "actual owner pin");
        var artifacts = Json.obj(Json.object(Json.parse(Files.readString(result))), "artifacts");
        List<Object> proof = new ArrayList<>();
        int classes = 0;

        for (String component : List.of("platform-callbacks", "window-compat", "mega-compat")) {
            var config =
                    Json.object(
                            Json.parse(
                                    Files.readString(
                                            root.resolve(
                                                    "profiles/"
                                                            + TestProfiles.pinning(
                                                                    root,
                                                                    PassionStandaloneLibrary.PIN)
                                                            + "/components/"
                                                            + component
                                                            + "/component.json"))));
            String outputName = Json.str(config, "output");
            Set<String> expected = PassionStandaloneLibrary.CLASSES.get(outputName);
            var artifact = Json.object(artifacts.get("component:" + component));
            Path original = Path.of(Json.str(artifact, "file"));
            check(
                    FilesSupport.hash(original).equals(Json.str(artifact, "sha256")),
                    "actual baseline JAR pin");
            String red = verify(tool, original, false);
            Files.writeString(stage.resolve(component + "-RED.txt"), red);
            byte[] before = NativeCore.entries(original).get("classes.dex");
            check(
                    PassionStandaloneLibrary.selected(
                            30, PassionStandaloneLibrary.PIN, outputName, expected),
                    "finite exact scope");
            check(
                    !PassionStandaloneLibrary.selected(30, "0".repeat(64), outputName, expected),
                    "other originals retain byte path unchanged");
            int rejected = 0;

            for (int mode : new int[] {0, 2, 3}) {
                try {
                    PassionStandaloneLibrary.selected(
                            mode == 0 ? 28 : 30,
                            PassionStandaloneLibrary.PIN,
                            mode == 2 ? "unrelated.jar" : outputName,
                            mode == 3 ? Set.of() : expected);
                } catch (Failure failure) {
                    check(
                            failure.code.equals("PASSION_LIBRARY_METADATA"),
                            "specific scope failure");
                    rejected++;
                }
            }

            check(rejected == 3, "all finite scope negatives");
            byte[] after = PassionStandaloneLibrary.apply(before, expected);
            Map<String, ClassDef> types = new TreeMap<>();

            for (ClassDef c : new DexBackedDexFile(Opcodes.forApi(30), after).getClasses()) {
                types.put(c.getType(), c);
            }

            for (ClassDef c : new DexBackedDexFile(Opcodes.forApi(30), before).getClasses()) {
                check(
                        FrameworkDexMaterializer.semantic(
                                        PassionStandaloneLibrary.withoutBootMetadata(c))
                                .equals(FrameworkDexMaterializer.semantic(types.get(c.getType()))),
                        "all code/access/field/static/debug/annotation semantics unchanged except"
                                + " hidden flags");
                classes++;
            }

            Path output = stage.resolve(outputName);
            NativeCore.zip(output, Map.of("classes.dex", after), false);
            String green = verify(tool, output, true);
            Files.writeString(stage.resolve(component + "-GREEN.txt"), green);
            proof.add(
                    Map.of(
                            "component",
                            component,
                            "class_count",
                            expected.size(),
                            "before_sha256",
                            FilesSupport.hash(original),
                            "output_sha256",
                            FilesSupport.hash(output),
                            "ART_RED_GREEN",
                            true,
                            "code_access_semantics_unchanged",
                            true,
                            "scope_negatives",
                            4));
        }

        Files.writeString(
                stage.resolve("receipt.json"),
                Json.write(
                                Map.of(
                                        "schema",
                                        1,
                                        "owner_sha256",
                                        PassionStandaloneLibrary.PIN,
                                        "dexdump_sha256",
                                        FilesSupport.hash(tool),
                                        "classes",
                                        classes,
                                        "components",
                                        proof,
                                        "hidden_api_boot_metadata_only",
                                        true,
                                        "guest_started",
                                        false,
                                        "PowerAdapter_note",
                                        "Existing exact two Power unsupported bodies already"
                                                + " present before normalization and remain"
                                                + " byte-semantically unchanged"))
                        + "\n");
        System.out.println(
                "actual3 pinned ART RED->GREEN, "
                        + classes
                        + " classes exact code/access and12 scope negatives; guest_started=false");
    }
}
