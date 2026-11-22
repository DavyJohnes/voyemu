package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.zip.ZipFile;

/** The API 28 Camera recipe keeps the OEM DVR view for the DVR entry of the original Camera. */
public final class CameraRecipeDvrEntryTest {
    private static final String ACTIVITY = "Lcom/qinggan/camera/activity/RecordVideoActivity;";
    private static final String STORE = ACTIVITY + "->mInType:I";
    private static final String SET_TYPE =
            "Lcom/qinggan/camera/common/CameraSetting;->setCameraType(I)V";

    private static void check(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    private static final class Target implements Recipes.Target {
        final Path root, work;
        final DexClasses classes;

        Target(Path root, Path work, DexClasses classes) {
            this.root = root;
            this.work = work;
            this.classes = classes;
        }

        @Override
        public int api() {
            return 28;
        }

        @Override
        public String abi() {
            return "arm64-v8a";
        }

        @Override
        public Path stepRoot() {
            return root;
        }

        @Override
        public Path work() {
            return work;
        }

        @Override
        public Path output() {
            return null;
        }

        @Override
        public DexClasses dex() {
            return classes;
        }

        @Override
        public Map<String, Path> uses() {
            return Map.of();
        }

        @Override
        public void addDex(String source) {}

        @Override
        public void putEntry(Path source, String name) {}

        @Override
        public void removeEntry(String name) {}

        @Override
        public void useLibraries(List<String> names) {}
    }

    private static List<Instruction> initIntent(DexClasses classes) {
        for (Method method : classes.get(ACTIVITY).getMethods()) {
            if (method.getName().equals("initIntent")) {
                var out = new ArrayList<Instruction>();
                method.getImplementation().getInstructions().forEach(out::add);

                return out;
            }
        }

        throw new AssertionError("initIntent missing");
    }

    private static String reference(Instruction instruction) {
        return instruction instanceof ReferenceInstruction r ? r.getReference().toString() : "";
    }

    /** Instructions from the read of the mInType extra up to its store in the OEM field. */
    private static List<Instruction> entry(List<Instruction> code) {
        for (int i = 0; i + 1 < code.size(); i++) {
            if (reference(code.get(i)).equals("mInType")
                    && reference(code.get(i + 1))
                            .equals(
                                    "Landroid/content/Intent;->getIntExtra(Ljava/lang/String;I)I")) {
                for (int j = i + 2; j < code.size(); j++) {
                    if (reference(code.get(j)).equals(STORE)) {
                        return code.subList(i + 2, j + 1);
                    }
                }
            }
        }

        throw new AssertionError("mInType entry missing");
    }

    // Argument: the original API 28 Camera.apk.
    public static void main(String[] arguments) throws Exception {
        Path apk = Path.of(arguments[0]);
        var archive = new TreeMap<String, byte[]>();
        var names = new ArrayList<String>();

        try (var zip = new ZipFile(apk.toFile())) {
            for (var e : java.util.Collections.list(zip.entries())) {
                if (e.getName().matches("classes[0-9]*\\.dex")) {
                    archive.put(e.getName(), zip.getInputStream(e).readAllBytes());
                    names.add(e.getName());
                }
            }
        }

        var original = entry(initIntent(new DexClasses(archive, names, 28)));
        check(
                original.size() == 2
                        && original.get(0).getOpcode() == Opcode.MOVE_RESULT
                        && ((OneRegisterInstruction) original.get(0)).getRegisterA() == 5,
                "original OEM entry store changed");
        Path project =
                Path.of(System.getProperty("voyemu.project.root", System.getProperty("user.dir")))
                        .toRealPath();
        Path parent = project.resolve(".local/build/tmp");
        Storage.directory(parent);
        Path stage = Files.createTempDirectory(parent, "camera-recipe-dvr-entry-test-");

        try {
            Path stepRoot = stage.resolve("patches"), work = stage.resolve("work");
            Files.createDirectories(stepRoot.resolve("camera-capabilities"));
            Files.createDirectories(work);
            Files.writeString(
                    stepRoot.resolve("camera-capabilities/voyemu-camera-capabilities.json"),
                    Json.write(
                            Map.of(
                                    "schema",
                                    1,
                                    "profile",
                                    TestProfiles.SYNTHETIC,
                                    "backend",
                                    "android-back-camera",
                                    "physicalDms",
                                    false,
                                    "physicalRpm",
                                    false)));
            var classes = new DexClasses(archive, names, 28);
            CameraRecipe.apply(new Target(stepRoot, work, classes));
            var adapted = entry(initIntent(classes));
            boolean typed = false;

            for (int i = 1; i < adapted.size() - 1; i++) {
                Instruction op = adapted.get(i);
                check(
                        !(op instanceof OneRegisterInstruction one
                                && one.getRegisterA() == 5
                                && op.getOpcode() != Opcode.IF_NE),
                        "DVR entry value rewritten before the OEM field store");

                if (reference(op).equals(SET_TYPE)) {
                    Instruction literal = adapted.get(i - 1);
                    typed =
                            literal instanceof NarrowLiteralInstruction value
                                    && value.getNarrowLiteral() == 2;
                }
            }

            check(typed, "DVR entry does not select the DVR camera type");
            check(
                    adapted.get(adapted.size() - 1).getOpcode() == Opcode.IPUT
                            && reference(adapted.get(adapted.size() - 1)).equals(STORE),
                    "DVR entry not stored in the OEM field");
            System.out.println("PASS API 28 DVR entry keeps the OEM DVR view and DVR camera type");
        } finally {
            FilesSupport.removeOwnedTree(stage);
        }
    }
}
