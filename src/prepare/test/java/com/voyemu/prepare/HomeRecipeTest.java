package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.List;
import java.util.Map;

/** Manual check of the A9 UserCenter home preference bridge on the original OEM APK. */
public final class HomeRecipeTest {
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

    public static void main(String[] args) throws Exception {
        Target target = new Target(Path.of(args[0]).toRealPath());
        String utils = "Lcom/qinggan/app/common/utils/AppUtils;";
        String old =
                "Landroid/content/pm/PackageManager;->addPreferredActivity(Landroid/content/IntentFilter;I[Landroid/content/ComponentName;Landroid/content/ComponentName;)V";

        if (!text(target.classes, utils).contains(old)) {
            throw new AssertionError("OEM invocation absent");
        }

        Recipes.apply(
                "usercenter-network/usercenter-network.json",
                Map.of("constants", Map.of(), "calls", List.of()),
                target);
        String after = text(target.classes, utils),
                bridge =
                        "Lcom/voyemu/oem/HomePreference;->add(Landroid/content/pm/PackageManager;Landroid/content/IntentFilter;I[Landroid/content/ComponentName;Landroid/content/ComponentName;)V";

        if (!"src".equals(target.helper)
                || after.contains(old)
                || !after.contains(bridge)
                || !after.contains("->clearPackagePreferredActivities(Ljava/lang/String;)V")) {
            throw new AssertionError("recipe bridge differs");
        }

        System.out.println("PASS home preference bridge on original OEM classes");
    }
}
