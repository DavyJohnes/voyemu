package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

/** Manual check of the A9 Launcher wireless-charging recipe on the original OEM APK. */
public final class WirelessRecipeTest {
    static final String BAR = "Lcom/qinggan/mainlauncher/hiboard/HiboardBar;";

    static final class Target implements Recipes.Target {
        final DexClasses classes;
        String helper;

        Target(Path apk) throws Exception {
            Map<String, byte[]> archive = new java.util.TreeMap<>(NativeCore.entries(apk));

            classes =
                    new DexClasses(
                            archive,
                            archive.keySet().stream()
                                    .filter(name -> name.matches("classes[0-9]*\\.dex"))
                                    .sorted()
                                    .toList(),
                            28);
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
            throw new AssertionError();
        }

        @Override
        public DexClasses dex() {
            return classes;
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
        public void putEntry(Path p, String n) {
            throw new AssertionError();
        }

        @Override
        public void removeEntry(String n) {
            throw new AssertionError();
        }

        @Override
        public void useLibraries(List<String> n) {
            throw new AssertionError();
        }
    }

    /** The instructions of every method of a class as smali lines. */
    static String text(DexClasses classes, String type) {
        StringBuilder result = new StringBuilder();

        for (var method : classes.get(type).getMethods()) {
            var code = method.getImplementation();

            if (code != null) {
                result.append(
                                String.join(
                                        "\n",
                                        DexEdits.render(
                                                code,
                                                new DexText.Frame(
                                                        code.getRegisterCount(),
                                                        DexEdits.parameters(method)))))
                        .append('\n');
            }
        }

        return result.toString();
    }

    public static void main(String[] a) throws Exception {
        Target target = new Target(Path.of(a[0]).toRealPath());
        var recipe = Json.object(Json.parse(Files.readString(Path.of(a[1]))));
        var routing = Json.list(Json.obj(recipe, "constants").get("ROUTING"));
        List<Object> chosen =
                routing.stream()
                        .filter(x -> ((String) Json.list(x).get(0)).startsWith(BAR + "->"))
                        .toList();

        Recipes.apply(
                "launcher-display-routing/launcher-display-routing.json",
                Map.of("constants", Map.of("ROUTING", chosen), "calls", List.of()),
                target);

        if (!"src".equals(target.helper)) {
            throw new AssertionError("helper changed");
        }

        var bar = target.classes.get(BAR);
        boolean released = false, ready = false;

        for (var field : bar.getFields()) {
            released |=
                    field.getName().equals("voyemuReleased")
                            && (field.getAccessFlags() & 0x40) != 0;
            ready |=
                    field.getName().equals("mCanBusManagerReady")
                            && (field.getAccessFlags() & 0x40) != 0;
        }

        if (!bar.getInterfaces().contains("Ljava/lang/Runnable;")
                || !released
                || !ready
                || !text(target.classes, BAR).contains(":cond_")
                || !text(target.classes, BAR).contains("->setupWirelessCharging()V")) {
            throw new AssertionError("routed owner differs");
        }

        if (!text(target.classes, "Lcom/qinggan/mainlauncher/hiboard/HiboardBar$5;")
                .contains(
                        "Lcom/qinggan/launcher/base/thread/WorkerThread;->postOnUiThread(Ljava/lang/Runnable;)V")) {
            throw new AssertionError("listener differs");
        }

        System.out.println("PASS production recipe on original OEM classes");
    }
}
