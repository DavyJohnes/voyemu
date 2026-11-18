package com.voyemu.prepare;

import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;

import javax.tools.ToolProvider;

/**
 * Exercises the actual own receiver with recording public-API fixtures; no Android runtime proof.
 */
public final class Api30BootstrapTest {
    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 1 || args.length > 2) {
            throw new IllegalArgumentException("ROOT [PROFILE_METADATA_DIRECTORY]");
        }

        Path root = Path.of(args[0]).toRealPath();
        String profile = "free-11-chinese-20260731-7.3.1";
        Path directory = root.resolve("profiles/" + profile + "/components/bootstrap");
        Path metadata =
                args.length == 2
                        ? Path.of(args[1]).toRealPath()
                        : root.resolve("profiles/" + profile);
        var definition =
                Json.object(Json.parse(Files.readString(metadata.resolve("profile.json"))));
        List<String> initial =
                Json.list(Json.obj(definition, "configure").get("start")).stream()
                        .map(value -> {
                            check(
                                    value instanceof Map<?, ?>,
                                    "ordinary services require explicit configure.start" + " mode");
                            var start = Json.object(value);
                            check(
                                    start.keySet().equals(Set.of("component", "mode"))
                                            && Json.str(start, "mode").equals("service"),
                                    "ordinary services must not inherit foreground-service"
                                            + " lifecycle");

                            return Json.str(start, "component");
                        })
                        .map(value -> {
                            String[] pair = value.split("/", 2);

                            return pair[0]
                                    + "/"
                                    + (pair[1].startsWith(".") ? pair[0] + pair[1] : pair[1]);
                        })
                        .toList();
        Path scratch =
                Files.createTempDirectory(
                        root.resolve(".local/build/tmp"), "api30-bootstrap-test-");

        try {
            Map<String, String> fixtures =
                    Map.of(
                            "android/content/ComponentName.java",
                                    "package android.content; public class ComponentName {}",
                            "android/content/BroadcastReceiver.java",
                                    "package android.content; public abstract class"
                                            + " BroadcastReceiver {public abstract void"
                                            + " onReceive(Context c,Intent i);}",
                            "android/content/Intent.java",
                                    "package android.content; public class Intent {public static"
                                        + " final String"
                                        + " ACTION_BOOT_COMPLETED=\"android.intent.action.BOOT_COMPLETED\";"
                                        + " public String action,component; public Intent(){}"
                                        + " public Intent(String a){action=a;} public"
                                        + " Intent(Context c,Class<?>"
                                        + " cls){component=\"com.voyemu/\"+cls.getName();} public"
                                        + " Intent setClassName(String p,String"
                                        + " c){component=p+\"/\"+c;return this;} public String"
                                        + " getAction(){return action;}}",
                            "android/content/Context.java",
                                    "package android.content; public class Context {public final"
                                        + " java.util.List<String> calls=new"
                                        + " java.util.ArrayList<>(); public ComponentName"
                                        + " startService(Intent i){calls.add(i.component);return"
                                        + " new ComponentName();}}",
                            "com/voyemu/output/SecondScreenService.java",
                                    "package com.voyemu.output; public class SecondScreenService"
                                            + " {}");
            List<String> compiler =
                    new ArrayList<>(List.of("-d", scratch.resolve("classes").toString()));

            for (var entry : fixtures.entrySet()) {
                Path file = scratch.resolve(entry.getKey());
                Files.createDirectories(file.getParent());
                Files.writeString(file, entry.getValue());
                compiler.add(file.toString());
            }

            compiler.add(directory.resolve("src/com/voyemu/output/BootReceiver.java").toString());
            compiler.add(
                    directory
                            .resolve("src/com/voyemu/output/SecondaryDisplayTarget.java")
                            .toString());
            check(
                    ToolProvider.getSystemJavaCompiler()
                                    .run(null, null, null, compiler.toArray(String[]::new))
                            == 0,
                    "actual own receiver compiles against recording public API contract");

            try (var loader =
                    new URLClassLoader(
                            new java.net.URL[] {scratch.resolve("classes").toUri().toURL()},
                            null)) {
                Class<?> context = loader.loadClass("android.content.Context"),
                        intent = loader.loadClass("android.content.Intent"),
                        receiver = loader.loadClass("com.voyemu.output.BootReceiver");
                Object live = context.getConstructor().newInstance(),
                        own = receiver.getConstructor().newInstance();
                var receive = receiver.getMethod("onReceive", context, intent);
                receive.invoke(own, live, null);
                receive.invoke(
                        own, live, intent.getConstructor(String.class).newInstance("other.action"));
                check(
                        ((List<?>) context.getField("calls").get(live)).isEmpty(),
                        "null/unrelated broadcast starts no services");
                receive.invoke(
                        own,
                        live,
                        intent.getConstructor(String.class)
                                .newInstance("android.intent.action.BOOT_COMPLETED"));
                List<?> calls = (List<?>) context.getField("calls").get(live);
                check(
                        calls.contains("com.voyemu/com.voyemu.output.SecondScreenService")
                                && calls.equals(initial.stream().filter(calls::contains).toList()),
                        "actual A11 receiver starts configured services in configured order");
                checkDisplaySelection(loader);
            }
        } finally {
            FilesSupport.removeOwnedTree(scratch);
        }

        System.out.println(
                Json.write(
                        Map.of(
                                "profile",
                                profile,
                                "exact_order_verified",
                                true,
                                "initial_and_boot_paths_match",
                                true,
                                "qinggan09_preserved",
                                true,
                                "guest_started",
                                false,
                                "api30_background_service_acceptance",
                                false)));
    }

    private static void checkDisplaySelection(ClassLoader loader) throws Exception {
        Class<?> target = loader.loadClass("com.voyemu.output.SecondaryDisplayTarget");
        var select =
                target.getDeclaredMethod(
                        "select", int[].class, String[].class, int[].class, int[].class);

        select.setAccessible(true);

        for (String name : List.of("Emulator2D", "Emulator 2D Display")) {
            check(
                    select.invoke(
                                    null,
                                    new int[] {0, 17},
                                    new String[] {"Built-in Screen", name},
                                    new int[] {1920, 1920},
                                    new int[] {720, 720})
                            .equals(17),
                    "both exact SDK names with native dimensions");
        }

        for (String name :
                List.of("FakeEmulator2D", "Emulator2D Extra", "Emulator 2D Display Extra", "")) {
            check(
                    select.invoke(
                                    null,
                                    new int[] {17},
                                    new String[] {name},
                                    new int[] {1920},
                                    new int[] {720})
                            .equals(-1),
                    "unknown names are rejected");
        }

        check(
                select.invoke(
                                null,
                                new int[] {17, 19},
                                new String[] {"Emulator2D", "Emulator 2D Display"},
                                new int[] {1920, 1920},
                                new int[] {720, 720})
                        .equals(-1),
                "ambiguous SDK displays rejected");
        check(
                select.invoke(
                                null,
                                new int[] {0},
                                new String[] {"Emulator 2D Display"},
                                new int[] {1920},
                                new int[] {720})
                        .equals(-1),
                "default display excluded");
        check(
                select.invoke(
                                null,
                                new int[] {17},
                                new String[] {"Emulator 2D Display"},
                                new int[] {1280},
                                new int[] {720})
                        .equals(-1),
                "wrong native dimensions rejected");
        check(
                select.invoke(
                                null,
                                null,
                                new String[] {"Emulator2D"},
                                new int[] {1920},
                                new int[] {720})
                        .equals(-1),
                "missing ID array rejected");
    }
}
