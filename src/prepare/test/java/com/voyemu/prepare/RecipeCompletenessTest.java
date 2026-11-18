package com.voyemu.prepare;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

public final class RecipeCompletenessTest {
    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static class RoutingTarget implements Recipes.Target {
        final Path work;
        final DexClasses classes;
        String helper;

        RoutingTarget(Path work, DexClasses classes) {
            this.work = work;
            this.classes = classes;
        }

        @Override
        public DexClasses dex() {
            return classes;
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
        public Path work() {
            return work;
        }

        @Override
        public Path output() {
            return null;
        }

        @Override
        public Map<String, Path> uses() {
            return Map.of();
        }

        @Override
        public void addDex(String source) {
            helper = source;
        }

        @Override
        public void putEntry(Path source, String name) {
            throw new AssertionError("unexpected entry");
        }

        @Override
        public void removeEntry(String name) {
            throw new AssertionError("unexpected removal");
        }

        @Override
        public void useLibraries(List<String> names) {
            throw new AssertionError("unexpected libraries");
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            throw new IllegalArgumentException("OWNED_WORK_DIRECTORY required");
        }

        Path parent = Path.of(args[0]).toRealPath();
        Path root = Files.createTempDirectory(parent, "recipe-completeness-");

        try {
            checkMediaDispatch(root);
            checkSystemUiRouting(root);
            System.out.println("media dispatch, method guards and routing behavior passed");
        } finally {
            FilesSupport.removeOwnedTree(root);
        }
    }

    /** One DEX entry with a class whose methods are given as name, registers and smali lines. */
    static DexClasses classes(String type, Object... methods) throws Exception {
        org.jf.dexlib2.iface.ClassDef definition =
                new org.jf.dexlib2.immutable.ImmutableClassDef(
                        type, 1, "Ljava/lang/Object;", null, null, null, null, null);

        for (int i = 0; i < methods.length; i += 3) {
            @SuppressWarnings("unchecked")
            List<String> lines = (List<String>) methods[i + 2];
            definition =
                    DexEdits.addMethod(
                            definition, (String) methods[i], 1, (Integer) methods[i + 1], lines);
        }

        Map<String, byte[]> archive =
                new java.util.TreeMap<>(
                        Map.of(
                                "classes.dex",
                                FrameworkDexMaterializer.serialize(
                                        new org.jf.dexlib2.immutable.ImmutableDexFile(
                                                org.jf.dexlib2.Opcodes.forApi(28),
                                                List.of(definition)))));

        return new DexClasses(archive, List.of("classes.dex"), 28);
    }

    /** The instructions of every method of a class as smali lines. */
    static List<String> lines(DexClasses classes, String type) {
        List<String> result = new ArrayList<>();

        for (var method : classes.get(type).getMethods()) {
            var code = method.getImplementation();
            result.addAll(
                    DexEdits.render(
                            code,
                            new DexText.Frame(
                                    code.getRegisterCount(), DexEdits.parameters(method))));
        }

        return result;
    }

    static final class MediaTarget extends RoutingTarget {
        final String original;
        final List<String> events = new ArrayList<>();
        String selectedAbi = "arm64-v8a";

        MediaTarget(Path root, String original, DexClasses classes) {
            super(root, classes);
            this.original = original;
        }

        @Override
        public String abi() {
            return selectedAbi;
        }

        @Override
        public String originalSha256() {
            return original;
        }

        @Override
        public void useLibraries(List<String> names) {
            events.add("libraries:" + names);
        }

        @Override
        public void mergeClasses(String reference, Map<String, Object> pins) {
            events.add("classes:" + reference + ":" + pins);
        }

        @Override
        public void useServices(List<String> names) {
            events.add("services:" + names);
        }

        @Override
        public void removeEntry(String name) {
            events.add("remove:" + name);
        }
    }

    static void checkMediaDispatch(Path root) throws Exception {
        String application = "Lcom/qinggan/media/MediaApplication;";
        String owner = NativeCore.hash("owner fixture".getBytes(StandardCharsets.UTF_8));
        var contract =
                Map.of(
                        "schema", 1,
                        "original_sha256", owner,
                        "shared_libraries", List.of("QGAPI", "QGMOD", "QGVehicle"),
                        "browser_classes", Map.of(),
                        "added_browser_services", List.of("fixture.MediaBrowser"),
                        "removed_arm32_libraries", List.of("lib/armeabi-v7a/libfixture.so"));
        var metadata =
                Map.<String, Object>of(
                        "contract",
                        contract,
                        "constants",
                        Map.of(
                                "applicationEdit",
                                List.of(
                                        application + "->onCreate()V",
                                        Map.of(
                                                "registers",
                                                2,
                                                "body",
                                                List.of("const/4 v0, 0x1", "return-void")))));
        DexClasses classes = classes(application, "onCreate()V", 1, List.of("return-void"));
        MediaTarget target = new MediaTarget(root, owner, classes);

        Recipes.apply("media-runtime.json", metadata, target);
        check(
                lines(classes, application).equals(List.of("const/4 v0, 0x1", "return-void")),
                "media dispatcher did not apply output");
        check(
                target.events.equals(
                        List.of(
                                "libraries:[QGAPI, QGMOD, QGVehicle]",
                                "classes:component:media:{}",
                                "services:[fixture.MediaBrowser]",
                                "remove:lib/armeabi-v7a/libfixture.so")),
                "media dispatch contract differs");

        for (int mode = 0; mode < 2; mode++) {
            DexClasses untouched = classes(application, "onCreate()V", 1, List.of("return-void"));
            MediaTarget bad = new MediaTarget(root, mode == 0 ? "0".repeat(64) : owner, untouched);

            if (mode == 1) {
                bad.selectedAbi = "armeabi-v7a";
            }

            boolean rejected = false;

            try {
                Recipes.apply("media-runtime.json", metadata, bad);
            } catch (Failure expectedFailure) {
                rejected = true;
            }

            check(
                    rejected
                            && bad.events.isEmpty()
                            && lines(untouched, application).equals(List.of("return-void")),
                    "foreign media input must fail before mutation");
        }
    }

    static void checkSystemUiRouting(Path root) throws Exception {
        String phone = "Lcom/qinggan/systemui/statusbar/phone/PhoneStatusBar;";
        String manager = "Landroid/view/WindowManager;->",
                add =
                        manager
                                + "addView(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                remove = manager + "removeView(Landroid/view/View;)V";
        DexClasses classes =
                classes(
                        phone,
                        "addStatusBarWindow(Z)V",
                        4,
                        List.of(
                                "invoke-interface {p1, v0, v1}, " + add,
                                "invoke-interface {p1, v0}, " + remove,
                                "return-void"),
                        "addStatusBarWindowSecond(Z)V",
                        4,
                        List.of(
                                "invoke-interface {p1, v0, v1}, " + add,
                                "invoke-interface {p1, v0}, " + remove,
                                "return-void"),
                        "showMuteIcon(Z)V",
                        4,
                        List.of("invoke-interface {p1, v0, v1}, " + add, "return-void"),
                        "setNavigationBarVisibility(Z)V",
                        6,
                        List.of(
                                "invoke-interface {v2, v3, v1}, " + add,
                                "invoke-interface {v1, v2}, " + remove,
                                "return-void"),
                        "destroy()V",
                        3,
                        List.of(
                                "invoke-interface {v0, v1}, "
                                        + manager
                                        + "removeViewImmediate(Landroid/view/View;)V",
                                "return-void"));
        RoutingTarget target = new RoutingTarget(root, classes);

        Recipes.apply(
                "systemui-display-routing.json",
                Map.of("constants", Map.of(), "calls", List.of()),
                target);
        List<String> routed = lines(classes, phone);
        check(
                routed.stream().noneMatch(line -> line.startsWith("invoke-interface"))
                        && routed.stream()
                                        .filter(line ->
                                                line.startsWith("invoke-static")
                                                        && line.contains(
                                                                "Lcom/voyemu/output/WindowRouter;"))
                                        .count()
                                == 8
                        && "launcher-display-routing/src".equals(target.helper),
                "SystemUI dynamic routing coverage");
        boolean reapplyRefused = false;

        try {
            Recipes.apply("systemui-display-routing.json", Map.of("constants", Map.of()), target);
        } catch (Failure expected) {
            reapplyRefused = expected.code.equals("DEX_EDIT_ANCHOR");
        }

        check(
                reapplyRefused && lines(classes, phone).equals(routed),
                "routing reapply must fail unchanged");
        RoutingTarget foreign =
                new RoutingTarget(root, classes) {
                    @Override
                    public int api() {
                        return 29;
                    }
                };
        boolean apiRefused = false;

        try {
            Recipes.apply("systemui-display-routing.json", Map.of("constants", Map.of()), foreign);
        } catch (Failure expected) {
            apiRefused = expected.code.equals("PATCH_API_UNSUPPORTED");
        }

        check(
                apiRefused && lines(classes, phone).equals(routed),
                "foreign routing API must fail unchanged");
    }
}
