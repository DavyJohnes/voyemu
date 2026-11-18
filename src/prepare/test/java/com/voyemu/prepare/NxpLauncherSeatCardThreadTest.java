package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Edits the real OEM SeatCard/hiboard window callbacks with the production recipe: launcher
 * SeatCard worker-thread view crash regression.
 */
public final class NxpLauncherSeatCardThreadTest {
    static void need(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static List<String> references(org.jf.dexlib2.iface.ClassDef type) {
        var refs = new ArrayList<String>();

        for (var method : type.getMethods()) {
            if (method.getImplementation() != null) {
                for (var instruction : method.getImplementation().getInstructions()) {
                    if (instruction instanceof ReferenceInstruction ref) {
                        refs.add(ref.getReference().toString());
                    }
                }
            }
        }

        return refs;
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), stage = Path.of(args[1]);

        Files.createDirectories(stage);
        String type = "Lcom/qinggan/app/launcher/LauncherModel$6;";
        var dex =
                new DexBackedDexFile(
                        Opcodes.forApi(28),
                        NativeCore.entries(
                                        root.resolve(
                                                ".local/profiles/free-09-chinese-20221230-nxp-rc45/originals/launcher.apk"))
                                .get("classes.dex"));
        var original =
                dex.getClasses().stream()
                        .filter(c -> c.getType().equals(type))
                        .findFirst()
                        .orElseThrow();
        var recipe =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve(
                                                "profiles/free-09-chinese-20221230-nxp-rc45/patches/nxp-launcher-display-routing.json"))));
        var edits = new ArrayList<DexEdits.MethodEdit>();

        for (Object entry : Json.list(recipe.get("calls"))) {
            var call = Json.object(entry);

            if ("editMethod".equals(call.get("operation"))) {
                var edit = DexEdits.MethodEdit.parse(Json.list(call.get("args")));

                if (edit.type().equals(type)) {
                    edits.add(edit);
                }
            }
        }

        need(
                !edits.isEmpty(),
                "OEM hiboard SeatCard callbacks still dispatched to launcher-loader; missing MAIN"
                        + " recipe");
        // RED anchor: the original must still hold the worker-thread dispatch for this class
        need(
                references(original).stream().anyMatch(r -> r.contains("runOnWorkerThread")),
                "original OEM hiboard callback lost worker dispatch (RED precondition changed)");
        var changed = DexEdits.apply(original, edits);
        int main = 0, unchanged = 0;

        for (var method : changed.getMethods()) {
            if (!Set.of("onWindowStartExpanded", "onWindowFullyCollapsed")
                    .contains(method.getName())) {
                unchanged++;
                continue;
            }

            var refs = new ArrayList<String>();

            for (var instruction : method.getImplementation().getInstructions()) {
                if (instruction instanceof ReferenceInstruction ref) {
                    refs.add(ref.getReference().toString());
                }
            }

            need(
                    refs.contains("Landroid/os/Looper;->getMainLooper()Landroid/os/Looper;"),
                    "callback does not resolve MAIN Looper");
            need(
                    refs.contains("Landroid/os/Handler;->post(Ljava/lang/Runnable;)Z"),
                    "callback not posted to explicit MAIN Handler");
            need(
                    refs.stream().noneMatch(r -> r.contains("runOnWorkerThread")),
                    "hiboard callback still invokes launcher-loader (CalledFromWrongThreadException"
                            + " source)");
            main++;
        }

        need(main == 2 && unchanged == 2, "unexpected method inventory");
        Files.writeString(
                stage.resolve("result.json"),
                Json.write(
                                Map.of(
                                        "real_oem_callback_dex",
                                        true,
                                        "main_dispatch_callbacks",
                                        main,
                                        "unchanged_methods",
                                        unchanged))
                        + "\n");
        System.out.println(
                "PASS NXP SeatCard OEM DEX: LauncherModel$6 hiboard callbacks dispatched on MAIN,"
                        + " worker dispatch removed");
    }
}
