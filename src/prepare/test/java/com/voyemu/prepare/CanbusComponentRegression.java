package com.voyemu.prepare;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.writer.io.MemoryDataStore;
import org.jf.dexlib2.writer.pool.DexPool;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayDeque;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.SortedSet;
import java.util.TreeMap;
import java.util.TreeSet;

/** Single real component through unchanged Artifacts; no full build, guest read or VM. */
public final class CanbusComponentRegression {
    static final String ID = "free-11-chinese-20260731-7.3.1";
    static int checks;

    static void need(boolean value, String reason) {
        checks++;

        if (!value) {
            throw new AssertionError(reason);
        }
    }

    static Map<String, ClassDef> dex(Path path) throws Exception {
        var result = new TreeMap<String, ClassDef>();
        var container = DexFileFactory.loadDexContainer(path.toFile(), Opcodes.forApi(30));

        for (String entry : container.getDexEntryNames()) {
            for (var c : container.getEntry(entry).getDexFile().getClasses()) {
                need(result.put(c.getType(), c) == null, "Duplicate DEX class " + c.getType());
            }
        }

        return result;
    }

    static void canModelAbi(Map<String, ClassDef> classes) {
        String prefix = "Lcom/qinggan/canbus/service/";

        for (String type :
                List.of(
                        "BaseCanBusComponent;",
                        "CanModelClient;",
                        "CanBusService;",
                        "protocol/dongfeng/DongfengCanBusComponentImpl;")) {
            need(classes.containsKey(prefix + type), "Shared CAN model class absent " + type);
        }

        need(
                (prefix + "RegistrationService;")
                        .equals(classes.get(prefix + "CanBusService;").getSuperclass()),
                "Model service must preserve registration lifecycle");
        ClassDef states = classes.get("Lcom/qinggan/canbus/VehicleState;");
        need(states != null, "OEM VehicleState missing");
        Map<Integer, Object> registers = new HashMap<>();
        Map<String, Integer> values = new TreeMap<>();

        for (var method : states.getDirectMethods()) {
            if ("<clinit>".equals(method.getName())) {
                for (var instruction : method.getImplementation().getInstructions()) {
                    if (instruction
                            instanceof
                            org.jf.dexlib2.iface.instruction.OneRegisterInstruction one) {
                        if (instruction
                                instanceof
                                org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction literal) {
                            registers.put(one.getRegisterA(), literal.getNarrowLiteral());
                        } else if (instruction
                                        instanceof
                                        org.jf.dexlib2.iface.instruction.ReferenceInstruction ref
                                && ref.getReference()
                                        instanceof
                                        org.jf.dexlib2.iface.reference.StringReference text) {
                            registers.put(one.getRegisterA(), text.getString());
                        }
                    }

                    if (instruction
                                    instanceof
                                    org.jf.dexlib2.iface.instruction.FiveRegisterInstruction args
                            && instruction
                                    instanceof
                                    org.jf.dexlib2.iface.instruction.ReferenceInstruction ref
                            && ref.getReference()
                                    instanceof org.jf.dexlib2.iface.reference.MethodReference target
                            && target.getDefiningClass().equals(states.getType())
                            && target.getName().equals("<init>")
                            && args.getRegisterCount() == 4) {
                        Object name = registers.get(args.getRegisterD()),
                                value = registers.get(args.getRegisterF());

                        if (name instanceof String string && value instanceof Integer i) {
                            values.put(string, i);
                        }
                    }
                }
            }
        }

        need(
                Integer.valueOf(665).equals(values.get("HUM_VSP_FUNCTION_SW")),
                "API30 PW numeric ABI differs from model protocol");
        need(
                Integer.valueOf(957).equals(values.get("IVI_SOC_MODESET")),
                "API30 FEV numeric ABI differs from model protocol");
    }

    static Set<String> refs(ClassDef c) throws Exception {
        var pool = new DexPool(Opcodes.forApi(30));

        pool.internClass(c);
        var bytes = new MemoryDataStore();
        pool.writeTo(bytes);
        var result = new TreeSet<String>();

        for (var type :
                new DexBackedDexFile(Opcodes.forApi(30), bytes.getData()).getTypeReferences()) {
            String value = type.getType();

            while (value.startsWith("[")) {
                value = value.substring(1);
            }

            if (value.startsWith("L")) {
                result.add(value);
            }
        }

        bytes.close();

        return result;
    }

    static String dump(Path aapt, Path apk) throws Exception {
        var process =
                new ProcessBuilder(
                                aapt.toString(),
                                "dump",
                                "xmltree",
                                apk.toString(),
                                "--file",
                                "AndroidManifest.xml")
                        .redirectErrorStream(true)
                        .start();
        String output =
                new String(
                        process.getInputStream().readAllBytes(),
                        java.nio.charset.StandardCharsets.UTF_8);

        need(process.waitFor() == 0, "AAPT manifest decode: " + output);

        return output;
    }

    static void manifest(String text) {
        need(text.contains("android.permission.INTERNET"), "INTERNET permission absent");
        boolean found = false;
        String[] lines = text.split("\\R", -1);

        for (int i = 0; i < lines.length; i++) {
            String line = lines[i];

            if (!line.stripLeading().startsWith("E: uses-library ")) {
                continue;
            }

            int indent = line.length() - line.stripLeading().length();
            var node = new StringBuilder();

            for (int j = i + 1; j < lines.length; j++) {
                String next = lines[j];
                int depth = next.length() - next.stripLeading().length();

                if (!next.isBlank() && depth <= indent) {
                    break;
                }

                node.append(next).append('\n');
            }

            String attributes =
                    node.toString()
                            .replace("http://schemas.android.com/apk/res/android:", "android:");

            if (attributes.matches("(?s).*A: android:name[^\\n]*=\"QGAPI\"[^\\n]*.*")) {
                need(!found, "Duplicate QGAPI shared library");
                found = true;
                need(
                        attributes.matches(
                                "(?s).*A: android:required[^\\n"
                                        + "]*=(?:\\(type 0x12\\))?(?:0xffffffff|true)(?:\\s|$).*"),
                        "QGAPI node must be required");
            }
        }

        need(found, "QGAPI shared library absent");
        need(!text.contains("QGVehicle"), "Embedded canbus ABI must not use QGVehicle library");
    }

    static String negativeManifest(String proposed) {
        String old =
                proposed.replaceAll(
                                "(?s)<uses-permission\\b[^>]*android:name=\"android.permission.INTERNET\"[^>]*/>",
                                "")
                        .replaceAll("(?s)<uses-library\\b[^>]*android:name=\"QGAPI\"[^>]*/>", "");

        need(
                !old.equals(proposed)
                        && !old.contains("android.permission.INTERNET")
                        && !old.contains("android:name=\"QGAPI\""),
                "Negative manifest must remove both runtime declarations");

        return old;
    }

    static void manifestSelfTest(Path retainedDump) throws Exception {
        String prefix =
                "E: manifest (line=1)\n"
                        + "  E: uses-permission (line=2)\n"
                        + "    A: android:name(0x01010003)=\"android.permission.INTERNET\" (Raw:"
                        + " \"android.permission.INTERNET\")\n"
                        + "  E: application (line=3)\n";
        String valid =
                prefix
                        + "    E: uses-library (line=4)\n"
                        + "      A: android:name(0x01010003)=\"QGAPI\" (Raw: \"QGAPI\")\n"
                        + "      A: android:required(0x0101028e)=(type 0x12)0xffffffff\n";

        manifest(valid);
        String actual = Files.readString(retainedDump);
        manifest(actual);
        boolean actualRejected = false;

        try {
            manifest(actual.replace("required(0x0101028e)=true", "required(0x0101028e)=false"));
        } catch (AssertionError good) {
            actualRejected = true;
        }

        need(actualRejected, "Actual expanded-namespace false required accepted");

        for (String broken :
                List.of(
                        valid.replace("0xffffffff", "0x0")
                                + "    E: uses-library (line=5)\n"
                                + "      A: android:name=\"Unrelated\"\n"
                                + "      A: android:required=(type 0x12)0xffffffff\n",
                        valid.replace("QGAPI", "Unrelated"))) {
            boolean rejected = false;

            try {
                manifest(broken);
            } catch (AssertionError good) {
                rejected = true;
            }

            need(rejected, "Manifest node mutation accepted");
        }

        String proposed =
                "<manifest><uses-permission android:name=\"android.permission.INTERNET\""
                        + " /><application><uses-library android:name=\"QGAPI\""
                        + " android:required=\"true\" /></application></manifest>";
        String frozen = negativeManifest(proposed);
        need(
                !frozen.contains("INTERNET") && !frozen.contains("QGAPI"),
                "Applied-manifest negative fixture changed");
        System.out.println("CanbusComponentRegression manifest self-test PASS");
    }

    public static void main(String[] args) throws Exception {
        if (args.length > 0 && args[0].equals("--manifest-self-test")) {
            need(args.length == 2, "Retained actual manifest dump path required");
            manifestSelfTest(Path.of(args[1]));

            return;
        }

        need(
                args.length == 4 || args.length == 5,
                "Arguments: root work existing-runtime-QGAPI.jar pinned-runtime-sha256"
                        + " [source-overlay-root]");
        Path root = Path.of(args[0]), job = Path.of(args[1]);
        Path execution = job.resolve("runs/" + java.time.Instant.now().toEpochMilli());
        Files.createDirectories(execution);
        Path canonical = root.resolve("profiles/" + ID),
                overlay = (args.length == 5 ? Path.of(args[4]) : root).resolve("profiles/" + ID);
        var data = Json.object(Json.parse(Files.readString(canonical.resolve("profile.json"))));
        var profile = new ProfileRegistry.Profile(ID, "isolated-canbus-regression", data, "", root);
        var storage =
                new Storage(
                        root.resolve(".local"),
                        execution.resolve("config"),
                        execution.resolve("cache"),
                        execution.resolve("state"),
                        execution.resolve("run"),
                        execution.resolve("logs"));
        Path donor =
                Inputs.originals(profile, storage)
                        .resolve(
                                Json.str(
                                        Json.obj(Json.obj(data, "original"), "MusicConcate"),
                                        "file"));
        Path qgapi =
                Inputs.originals(profile, storage)
                        .resolve(Json.str(Json.obj(Json.obj(data, "original"), "QGAPI"), "file"));
        NativeCore.pinned(
                donor, Json.str(Json.obj(Json.obj(data, "original"), "MusicConcate"), "sha256"));
        NativeCore.pinned(qgapi, Json.str(Json.obj(Json.obj(data, "original"), "QGAPI"), "sha256"));
        var tools = toolchains(root, storage);
        Path recipe = execution.resolve("recipe");
        FilesSupport.copyTree(
                canonical.resolve("components/canbus"), recipe.resolve("components/canbus"));

        for (var e : FilesSupport.tree(overlay.resolve("components/canbus")).entrySet()) {
            Path target = recipe.resolve("components/canbus").resolve(e.getKey());
            Files.createDirectories(target.getParent());
            Files.copy(e.getValue(), target, StandardCopyOption.REPLACE_EXISTING);
        }

        // Derive a stable negative fixture from the proposed manifest, including after canonical
        // apply.
        boolean rejected = checkNegativeManifest(execution, recipe, tools);
        var builder =
                new Artifacts(
                        new Artifacts.PatchedProfile(profile, Map.of()),
                        storage,
                        tools,
                        recipe,
                        execution.resolve("normal-artifacts"),
                        logical -> {
                            throw new AssertionError("Unexpected guest input " + logical);
                        },
                        System.err::println);
        Path output = builder.resolve("component:canbus");
        String decoded = dump(tools.buildTool("aapt2"), output);
        Files.writeString(execution.resolve("produced-manifest.txt"), decoded);
        manifest(decoded);
        need(
                args.length == 4 || args.length == 5,
                "Explicit existing installed patched QGAPI path and SHA256 required");
        Path runtimeQgapi = Path.of(args[2]);
        NativeCore.pinned(runtimeQgapi, args[3]);
        var runtimePatch = Json.obj(Json.obj(data, "patches"), "QGAPI");
        need("original:QGAPI".equals(runtimePatch.get("input")), "Runtime QGAPI donor mapping");
        need(
                Json.list(data.get("install")).stream()
                        .map(Json::object)
                        .anyMatch(e ->
                                "patched:QGAPI".equals(e.get("artifact"))
                                        && "/system/framework/QGAPI.jar".equals(e.get("target"))),
                "Actual installed QGAPI mapping");
        var own = dex(output);
        var shared = dex(runtimeQgapi);
        canModelAbi(own);
        var reflected =
                List.of(
                        "Lcom/qinggan/bluetooth/BTManager;",
                        "Lcom/qinggan/bluetooth/profile/BTAdapter;",
                        "Lcom/qinggan/bluetooth/profile/IBTCallback;",
                        "Lcom/qinggan/bluetooth/profile/Hfp;",
                        "Lcom/qinggan/bluetooth/profile/IHfpCallback;");
        checkOwnedClasses(own, shared, reflected);
        var all = new TreeMap<String, ClassDef>(shared);
        all.putAll(own);
        var visited = new TreeSet<String>();
        var external = new TreeSet<String>();
        var pending = new ArrayDeque<String>(own.keySet());
        pending.addAll(reflected);

        while (!pending.isEmpty()) {
            String type = pending.removeFirst();

            if (!visited.add(type)) {
                continue;
            }

            ClassDef c = all.get(type);

            if (c == null) {
                external.add(type);
                continue;
            }

            pending.addAll(refs(c));
        }

        var missing = missingTypes(external);
        Files.writeString(
                execution.resolve("external-types.txt"), String.join("\n", external) + "\n");
        Files.writeString(
                execution.resolve("unresolved-types.txt"), String.join("\n", missing) + "\n");
        checkMappingAndSignature(canonical, output);
        var receipt = new LinkedHashMap<String, Object>();
        receipt.put("ordinary_component_artifacts", true);
        receipt.put("old_manifest_rejected", rejected);
        receipt.put("apk", output.toString());
        receipt.put("apk_sha256", FilesSupport.hash(output));
        receipt.put(
                "runtime_qgapi",
                Map.of(
                        "file",
                        runtimeQgapi.toString(),
                        "sha256",
                        FilesSupport.hash(runtimeQgapi),
                        "install_artifact",
                        "patched:QGAPI"));
        receipt.put(
                "original_qgapi",
                Map.of("file", qgapi.toString(), "sha256", FilesSupport.hash(qgapi)));
        receipt.put(
                "music_donor",
                Map.of("file", donor.toString(), "sha256", FilesSupport.hash(donor)));
        receipt.put("owned_and_donor_classes", own.size());
        receipt.put("reachable_classes", visited.size());
        receipt.put("unresolved_external_types", missing);
        receipt.put("static_closure_sufficient", missing.isEmpty());
        receipt.put("runtime_verified", false);
        receipt.put("reflection_and_native_closure_verified", false);
        receipt.put("canonical_mutations", false);
        receipt.put("checks", checks);
        Files.writeString(execution.resolve("receipt.json"), Json.write(receipt) + "\n");
        Files.writeString(job.resolve("latest-run.txt"), execution + "\n");
        System.out.println(Json.write(receipt));
        need(missing.isEmpty(), "Nonframework transitive closure unresolved " + missing);
    }

    static Toolchains toolchains(Path root, Storage storage) throws Exception {
        var signing =
                Json.obj(
                        Json.object(
                                Json.parse(
                                        Files.readString(
                                                root.resolve(
                                                        ".local/profiles/"
                                                                + ID
                                                                + "/native-inputs.json")))),
                        "signing");
        Path key = Path.of(Json.str(Json.obj(signing, "key"), "file")),
                cert = Path.of(Json.str(Json.obj(signing, "cert"), "file"));

        NativeCore.pinned(key, Json.str(Json.obj(signing, "key"), "sha256"));
        NativeCore.pinned(cert, Json.str(Json.obj(signing, "cert"), "sha256"));
        var tools =
                new Toolchains(
                        storage,
                        Json.object(
                                Json.parse(
                                        Files.readString(root.resolve("src/app/toolchain.json")))),
                        root.resolve(".local/toolchains/native-sdk"),
                        key,
                        cert,
                        root.resolve(
                                ".local/toolchains/java-sdk/platforms/android-34/android.jar"));

        return tools;
    }

    static boolean checkNegativeManifest(Path execution, Path recipe, Toolchains tools)
            throws Exception {
        Path oldDir = execution.resolve("old-manifest");

        Files.createDirectories(oldDir);
        Path oldSource = execution.resolve("negative-component");
        FilesSupport.copyTree(recipe.resolve("components/canbus"), oldSource);
        Files.writeString(
                oldSource.resolve("AndroidManifest.xml"),
                negativeManifest(
                        Files.readString(recipe.resolve("components/canbus/AndroidManifest.xml"))));
        Path oldResources =
                Artifacts.linkComponentResources(
                        tools.buildTool("aapt2"), tools.platform(), oldSource, oldDir, 28, 28);
        String oldDump = dump(tools.buildTool("aapt2"), oldResources);
        Files.writeString(execution.resolve("old-manifest.txt"), oldDump);
        boolean rejected = false;

        try {
            manifest(oldDump);
        } catch (AssertionError expected) {
            rejected = true;
        }

        need(rejected, "Derived negative manifest should fail runtime declarations");

        return rejected;
    }

    static void checkOwnedClasses(
            Map<String, ClassDef> own, Map<String, ClassDef> shared, List<String> reflected) {
        for (String c : reflected) {
            need(shared.containsKey(c), "Actual runtime QGAPI missing " + c);
        }

        for (String c : own.keySet()) {
            need(
                    !c.startsWith("Lcom/qinggan/bluetooth/")
                            || c.startsWith("Lcom/qinggan/bluetooth/BTConst"),
                    "APK embeds runtime Bluetooth class " + c);
        }

        for (String c :
                List.of(
                        "HfpPhoneInfoBridge",
                        "HfpPhoneInfoValues",
                        "IpkFrames",
                        "IpkInfoSync",
                        "IpkLink",
                        "PhoneDeliveryDiagnostics")) {
            need(
                    own.containsKey("Lcom/qinggan/canbus/service/" + c + ";"),
                    "Owned bridge class missing " + c);
        }
    }

    static SortedSet<String> missingTypes(Set<String> external) {
        var missing = new TreeSet<String>();

        for (String type : external) {
            if (!(type.startsWith("Ljava/")
                    || type.startsWith("Ljavax/")
                    || type.startsWith("Landroid/")
                    || type.startsWith("Ldalvik/")
                    || type.startsWith("Lorg/xml/")
                    || type.startsWith("Lorg/w3c/")
                    || type.startsWith("Lorg/json/")
                    || type.startsWith("Lorg/apache/http/")
                    || type.startsWith("Lcom/android/"))) {
                missing.add(type);
            }
        }

        return missing;
    }

    static void checkMappingAndSignature(Path canonical, Path output) throws Exception {
        String mapping = Files.readString(canonical.resolve("files/voyemu-qinggan-framework.xml"));

        need(
                mapping.contains("name=\"QGAPI\" file=\"/system/framework/QGAPI.jar\""),
                "Runtime QGAPI mapping changed");
        var verify =
                new com.android.apksig.ApkVerifier.Builder(output.toFile())
                        .setMinCheckedPlatformVersion(30)
                        .setMaxCheckedPlatformVersion(30)
                        .build()
                        .verify();
        need(verify.isVerified(), "Platform-signed APK verify");
    }
}
