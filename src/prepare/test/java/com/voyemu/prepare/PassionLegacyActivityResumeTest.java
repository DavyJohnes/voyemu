package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.dexbacked.instruction.DexBackedInstruction;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Resolve the real secondary display recipe against the pinned clean Android 11 JAR. */
public final class PassionLegacyActivityResumeTest {
    private static final String INPUT = "android:/system/framework/services.jar";
    private static final String TYPE = "Lcom/android/server/wm/WindowProcessController;";
    private static final String METHOD =
            TYPE
                    + "->updateTopResumingActivityInProcessIfNeeded"
                    + "(Lcom/android/server/wm/ActivityRecord;)Z";

    private static ClassDef controller(byte[] raw) {
        return new DexBackedDexFile(Opcodes.forApi(30), raw)
                .getClasses().stream()
                        .filter(type -> type.getType().equals(TYPE))
                        .findFirst()
                        .orElseThrow();
    }

    private static Method selection(ClassDef controller) {
        for (var method : controller.getVirtualMethods()) {
            if (DexEdits.descriptor(method).equals(METHOD)) {
                return method;
            }
        }

        throw new AssertionError("Missing legacy resume selection");
    }

    private static void check(boolean value, String reason) {
        if (!value) {
            throw new AssertionError(reason);
        }
    }

    private static Path patch(
            Path recipe, Path jar, Path work, String name, String abi, String preparation)
            throws Exception {
        String step = PassionSecondaryNavigationGuard.NAME + ".json";
        var metadata = Json.object(Json.parse(Files.readString(recipe.resolve("patches/" + step))));

        metadata.put("sourceSha256", FilesSupport.hash(recipe.resolve("patches/" + step)));
        var node = Map.of("type", "jar", "input", INPUT, "steps", List.of(step));
        var data =
                Map.<String, Object>of(
                        "platform", "passion",
                        "board", "8155",
                        "android", Map.of("package", "system-images;android-30;default;" + abi),
                        "target", Map.of("name", name, "guestAbi", abi, "preparation", preparation),
                        "patches", Map.of(PassionSecondaryNavigationGuard.NAME, node));
        var profile = new ProfileRegistry.Profile(TestProfiles.SYNTHETIC, "test", data, "", recipe);
        var artifacts =
                new Artifacts(
                        new Artifacts.PatchedProfile(profile, Map.of(step, metadata)),
                        null,
                        null,
                        recipe,
                        work,
                        path -> {
                            check(
                                    path.equals(INPUT.substring("android:".length())),
                                    "Legacy resume must use the pinned SDK JAR");

                            return jar;
                        },
                        line -> {});

        return artifacts.resolve("patched:" + PassionSecondaryNavigationGuard.NAME);
    }

    private static void checkStockSelection(Method method) {
        var code = method.getImplementation();

        for (var instruction : code.getInstructions()) {
            if (instruction instanceof ReferenceInstruction reference) {
                check(
                        !reference.getReference().toString().contains("->startPausingLocked("),
                        "The x86 stock HOME pair must keep the previously resumed activity");
            }
        }

        int offset = 0, storesLegacyActivity = 0, nops = 0;

        for (var instruction : code.getInstructions()) {
            if (offset == 0x51) {
                check(
                        instruction.getOpcode() == Opcode.IF_EQZ
                                && ((OffsetInstruction) instruction).getCodeOffset() == 0x1b,
                        "The previous-activity switch remains conditional");
            }

            if (offset >= 0x53 && offset < 0x6a) {
                check(
                        instruction.getOpcode() == Opcode.NOP,
                        "Only the previous-activity pause is bypassed");
                nops++;
            }

            if (instruction instanceof ReferenceInstruction reference) {
                var target = reference.getReference().toString();

                if (instruction.getOpcode() == Opcode.IPUT_OBJECT
                        && target.endsWith(
                                "->mPreQTopResumedActivity:Lcom/android/server/wm/ActivityRecord;")) {
                    check(
                            offset == 0x6a,
                            "The selected activity must be stored at the original branch join");
                    storesLegacyActivity++;
                }
            }

            offset += instruction.getCodeUnits();
        }

        check(
                offset == 111 && storesLegacyActivity == 1 && nops == 23,
                "Other resume selection branches and the return path must remain intact");
    }

    private static void checkRawDelta(byte[] original) throws Exception {
        byte[] patched = LegacyActivityResumeGuard.run(original, true);
        var dex = new DexBackedDexFile(Opcodes.forApi(30), original);
        int address = 0, start = -1;

        for (var instruction :
                selection(controller(original)).getImplementation().getInstructions()) {
            if (address == 0x53) {
                start =
                        dex.getDataBuffer().getBaseOffset()
                                + ((DexBackedInstruction) instruction).instructionStart;
            }

            address += instruction.getCodeUnits();
        }

        check(start > 32 && patched.length == original.length, "The edit must preserve DEX layout");
        check(
                Arrays.equals(original, 0, 8, patched, 0, 8)
                        && Arrays.equals(original, 32, start, patched, 32, start)
                        && Arrays.equals(
                                original,
                                start + 46,
                                original.length,
                                patched,
                                start + 46,
                                patched.length),
                "Only pause instructions and DEX integrity fields may differ");
        checkStockSelection(selection(controller(patched)));
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), jar = Path.of(args[1]), work = Path.of(args[2]);
        var recipe =
                root.resolve("profiles")
                        .resolve(
                                TestProfiles.declaring(root, PassionSecondaryNavigationGuard.NAME));
        var original = NativeCore.entries(jar);
        var arm =
                NativeCore.entries(
                        patch(recipe, jar, work.resolve("arm"), "arm", "arm64-v8a", "sdk"));
        var x86 =
                NativeCore.entries(
                        patch(recipe, jar, work.resolve("x86"), "x86", "x86_64", "signed-offline"));
        var armController = controller(arm.get("classes2.dex"));
        var x86Controller = controller(x86.get("classes2.dex"));

        check(
                FrameworkDexMaterializer.semantic(armController)
                        .equals(
                                FrameworkDexMaterializer.semantic(
                                        controller(original.get("classes2.dex")))),
                "The ARM target must retain the original window process policy");
        checkStockSelection(selection(x86Controller));
        checkRawDelta(original.get("classes2.dex"));
        var armMethods = new LinkedHashMap<String, String>();

        for (var method : armController.getMethods()) {
            if (!DexEdits.descriptor(method).equals(METHOD)) {
                armMethods.put(
                        DexEdits.descriptor(method), FrameworkDexMaterializer.semantic(method));
            }
        }

        for (var method : x86Controller.getMethods()) {
            if (!DexEdits.descriptor(method).equals(METHOD)) {
                check(
                        FrameworkDexMaterializer.semantic(method)
                                .equals(armMethods.remove(DexEdits.descriptor(method))),
                        "An unrelated method in WindowProcessController changed");
            }
        }

        check(armMethods.isEmpty(), "An unrelated method in WindowProcessController disappeared");

        var foreign = new LinkedHashMap<>(original);
        foreign.put("foreign", new byte[] {1});
        Path foreignJar = work.resolve("foreign.jar");
        NativeCore.zip(foreignJar, foreign, false);

        try {
            patch(recipe, foreignJar, work.resolve("invalid"), "x86", "x86_64", "signed-offline");
            throw new AssertionError("An unpinned JAR must not reach the legacy resume editor");
        } catch (Failure expected) {
            check(
                    !Files.exists(
                            work.resolve(
                                    "invalid/patched-"
                                            + PassionSecondaryNavigationGuard.NAME
                                            + "/patched.jar")),
                    "A rejected input produced a JAR");
        }

        System.out.println("Pinned x86 legacy resume policy and unchanged ARM passed");
    }
}
