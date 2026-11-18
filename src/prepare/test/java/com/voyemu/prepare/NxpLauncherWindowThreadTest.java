package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableDexFile;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Edits the real OEM callback with the production declarative recipe. */
public final class NxpLauncherWindowThreadTest {
    static void need(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), stage = Path.of(args[1]);

        Files.createDirectories(stage);
        String type = "Lcom/qinggan/app/launcher/LauncherModel$4;";
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
                "OEM window callbacks still dispatched to launcher-loader; missing MAIN recipe");
        var changed = DexEdits.apply(original, edits);
        byte[] output =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(28), List.of(changed)));
        int callbacks = 0, unchanged = 0;

        for (var method : changed.getMethods()) {
            if (!Set.of("onWindowMoved", "onWindowFullyCollapsed").contains(method.getName())) {
                var old = original.getMethods().iterator();
                boolean equal = false;

                while (old.hasNext()) {
                    var candidate = old.next();

                    if (candidate.getName().equals(method.getName())) {
                        equal =
                                FrameworkDexMaterializer.semantic(candidate)
                                        .equals(FrameworkDexMaterializer.semantic(method));
                    }
                }

                need(equal, "unrelated OEM callback changed");
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
                    refs.contains("Landroid/os/Handler;-><init>(Landroid/os/Looper;)V")
                            && refs.contains("Landroid/os/Handler;->post(Ljava/lang/Runnable;)Z"),
                    "callback not posted to explicit MAIN Handler");
            need(
                    refs.stream().noneMatch(r -> r.contains("runOnWorkerThread")),
                    "UI callback still invokes launcher-loader");
            need(
                    refs.stream()
                            .anyMatch(r ->
                                    r.contains(
                                            method.getName().equals("onWindowMoved")
                                                    ? "LauncherModel$4$2;-><init>"
                                                    : "LauncherModel$4$1;-><init>")),
                    "original OEM Runnable not preserved");
            callbacks++;
        }

        need(callbacks == 2 && unchanged == 2, "unexpected method inventory");
        Files.writeString(
                stage.resolve("result.json"),
                Json.write(
                                Map.of(
                                        "real_oem_callback_dex",
                                        true,
                                        "main_dispatch_callbacks",
                                        callbacks,
                                        "unchanged_methods",
                                        unchanged,
                                        "original_runnables_preserved",
                                        true,
                                        "output_sha256",
                                        NativeCore.hash(output)))
                        + "\n");
        System.out.println(
                "PASS NXP actual OEM DEX: 2 MAIN dispatches, 2 unchanged methods, existing"
                        + " Runnables preserved");
    }
}
