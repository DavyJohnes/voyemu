package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

import javax.xml.parsers.DocumentBuilderFactory;

/** Exact NXP stock-bar overlay and OEM two-display routing; no guest interaction. */
public final class NxpStatusbarOverlayTest {
    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static List<String> lines(org.jf.dexlib2.iface.Method method) {
        var code = method.getImplementation();

        return DexEdits.render(
                code, new DexText.Frame(code.getRegisterCount(), DexEdits.parameters(method)));
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]),
                profile = root.resolve("profiles/free-09-chinese-20221230-nxp-rc45");
        Path overlay = profile.resolve("components/statusbar-overlay");
        var factory = DocumentBuilderFactory.newInstance();

        factory.setNamespaceAware(true);
        var manifest =
                factory.newDocumentBuilder().parse(overlay.resolve("AndroidManifest.xml").toFile());
        var declaration = (org.w3c.dom.Element) manifest.getElementsByTagName("overlay").item(0);
        String android = "http://schemas.android.com/apk/res/android";
        check(
                declaration.getAttributeNS(android, "targetPackage").equals("android"),
                "Overlay must target SDK framework, never OEM SystemUI");
        var resources =
                factory.newDocumentBuilder()
                        .parse(overlay.resolve("res/values/dimens.xml").toFile())
                        .getElementsByTagName("dimen");
        check(resources.getLength() == 3, "Only the A9-proven three stock status dimensions");
        var names = new java.util.HashSet<String>();

        for (int i = 0; i < resources.getLength(); i++) {
            var resource = (org.w3c.dom.Element) resources.item(i);
            names.add(resource.getAttribute("name"));
            check(resource.getTextContent().equals("0dp"), "No stock status inset");
        }

        check(
                names.equals(
                        java.util.Set.of(
                                "status_bar_height",
                                "status_bar_height_portrait",
                                "status_bar_height_landscape")),
                "No unrelated framework dimension");
        var recipe =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        profile.resolve(
                                                "patches/nxp-systemui-display-routing.json"))));
        List<Object> calls = Json.list(recipe.get("calls"));
        String owner = "Lcom/qinggan/systemui/statusbar/phone/PhoneStatusBar;";
        var edits = new java.util.ArrayList<DexEdits.MethodEdit>();

        for (Object value : calls) {
            Map<String, Object> call = Json.object(value);

            if (Json.str(call, "operation").equals("editMethod")) {
                var edit = DexEdits.MethodEdit.parse(Json.list(call.get("args")));

                if (edit.type().equals(owner)) {
                    edits.add(edit);
                }
            }
        }

        check(
                Json.str(Json.object(calls.get(0)), "operation").equals("editMethod")
                        && edits.stream().mapToInt(edit -> edit.edits().size()).sum() == 8,
                "All eight OEM add/remove sites");
        Path stage = Path.of(args[1]);
        Files.createDirectories(stage);
        Path originalApk =
                root.resolve(
                        ".local/profiles/free-09-chinese-20221230-nxp-rc45/originals/systemui.apk");
        check(
                NativeCore.hash(Files.readAllBytes(originalApk))
                        .equals(Json.str(Json.obj(recipe, "constants"), "INPUT_SHA256")),
                "Exact pinned original NXP SystemUI APK");
        var dex =
                new DexBackedDexFile(
                        Opcodes.forApi(28), NativeCore.entries(originalApk).get("classes.dex"));
        var originalClass =
                dex.getClasses().stream()
                        .filter(value -> value.getType().equals(owner))
                        .findFirst()
                        .orElseThrow();
        var changedClass = DexEdits.apply(originalClass, edits);
        Map<String, org.jf.dexlib2.iface.Method> changedMethods = new java.util.TreeMap<>();

        for (var method : changedClass.getMethods()) {
            changedMethods.put(DexEdits.descriptor(method), method);
        }

        int changes = 0;

        for (var method : originalClass.getMethods()) {
            var after = changedMethods.get(DexEdits.descriptor(method));

            if (method.getImplementation() == null) {
                continue;
            }

            List<String> original = lines(method), updated = lines(after);
            check(
                    original.size() == updated.size(),
                    "Same-width invocation edits preserve the instruction layout");

            for (int i = 0; i < original.size(); i++) {
                if (!original.get(i).equals(updated.get(i))) {
                    changes++;
                    check(
                            original.get(i).startsWith("invoke-interface")
                                    && original.get(i).contains("Landroid/view/WindowManager;->"),
                            "Original WindowManager invocation");
                    check(
                            updated.get(i).startsWith("invoke-static")
                                    && updated.get(i)
                                            .contains("Lcom/voyemu/output/WindowRouter;->"),
                            "Existing OEM-aware router");
                    check(
                            original.get(i)
                                    .substring(
                                            original.get(i).indexOf('{'),
                                            original.get(i).indexOf('}') + 1)
                                    .equals(
                                            updated.get(i)
                                                    .substring(
                                                            updated.get(i).indexOf('{'),
                                                            updated.get(i).indexOf('}') + 1)),
                            "Same receiver and view registers");
                }
            }
        }

        check(
                changes == 8,
                "Only eight instructions changed; every other OEM instruction preserved");
        boolean refused = false;

        try {
            DexEdits.apply(changedClass, edits);
        } catch (Failure failure) {
            refused = failure.code.equals("DEX_EDIT_ANCHOR");
        }

        check(refused, "Changed OEM class rejected by its anchors");
        var libraries = Json.object(calls.get(calls.size() - 1));
        check(
                Json.str(libraries, "operation").equals("useLibraries")
                        && Json.list(libraries.get("args"))
                                .equals(List.of(List.of("VoyemuLauncherRouting"))),
                "No duplicate router DEX; use existing installed framework library");
        System.out.println(
                "PASS exact stock framework overlay, eight OEM calls, whole-class inverse"
                        + " preservation, changed-class refusal; runtime shade acceptance pending");
    }
}
