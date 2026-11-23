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
            List<String> compiler =
                    new ArrayList<>(List.of("-d", scratch.resolve("classes").toString()));

            for (var entry : fixtures().entrySet()) {
                Path file = scratch.resolve(entry.getKey());
                Files.createDirectories(file.getParent());
                Files.writeString(file, entry.getValue());
                compiler.add(file.toString());
            }

            var androidDoubles = root.resolve("src/prepare/test/nxp-bootstrap/android");

            for (String file :
                    List.of(
                            "app/ActivityOptions.java",
                            "content/ComponentName.java",
                            "hardware/display/DisplayManager.java",
                            "os/Bundle.java",
                            "os/Handler.java",
                            "os/IBinder.java",
                            "os/Looper.java",
                            "util/Log.java")) {
                compiler.add(androidDoubles.resolve(file).toString());
            }

            for (String file :
                    List.of(
                            "BootReceiver.java",
                            "SecondScreenService.java",
                            "SecondaryDisplayTarget.java")) {
                compiler.add(directory.resolve("src/com/voyemu/output").resolve(file).toString());
            }

            check(
                    ToolProvider.getSystemJavaCompiler()
                                    .run(null, null, null, compiler.toArray(String[]::new))
                            == 0,
                    "actual receiver and second-screen service compile against recording public API"
                            + " contract");

            try (var loader =
                    new URLClassLoader(
                            new java.net.URL[] {scratch.resolve("classes").toUri().toURL()},
                            null)) {
                Class<?> context = loader.loadClass("android.content.Context"),
                        intent = loader.loadClass("android.content.Intent"),
                        receiver = loader.loadClass("com.voyemu.output.BootReceiver"),
                        service = loader.loadClass("com.voyemu.output.SecondScreenService");
                String setting =
                        "com.qinggan.app.setting/com.qinggan.app.setting.service.MainSettingService";
                Object explicitStart = service.getConstructor().newInstance();
                service.getMethod("onCreate").invoke(explicitStart);
                var start = service.getMethod("onStartCommand", intent, int.class, int.class);
                start.invoke(explicitStart, null, 0, 1);
                start.invoke(explicitStart, null, 0, 2);
                check(
                        context.getField("calls").get(explicitStart).equals(List.of(setting)),
                        "direct display bootstrap starts Settings once without a boot broadcast");

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
                                && !calls.contains(setting)
                                && initial.contains(setting)
                                && calls.equals(initial.stream().filter(calls::contains).toList()),
                        "boot keeps the remaining services in configured order and delegates"
                                + " Settings to the same display service");
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
                                false,
                                "explicit_bootstrap_starts_settings",
                                true)));
    }

    private static Map<String, String> fixtures() {
        return Map.of(
                "android/content/Context.java",
                """
                package android.content;

                public class Context {
                    public static final String DISPLAY_SERVICE = "display";
                    public final java.util.List<String> calls = new java.util.ArrayList<>();

                    public ComponentName startService(Intent intent) {
                        ComponentName component = intent.getComponent();
                        calls.add(component.getPackageName() + "/" + component.getClassName());
                        return component;
                    }

                    public Object getSystemService(String name) {
                        return null;
                    }
                }
                """,
                "android/content/BroadcastReceiver.java",
                """
                package android.content;

                public abstract class BroadcastReceiver {
                    public abstract void onReceive(Context context, Intent intent);
                }
                """,
                "android/content/Intent.java",
                """
                package android.content;

                public class Intent {
                    public static final String ACTION_BOOT_COMPLETED =
                            "android.intent.action.BOOT_COMPLETED";
                    public static final int FLAG_ACTIVITY_NEW_TASK = 0x10000000;
                    private ComponentName component;
                    private String action;

                    public Intent() {}
                    public Intent(String action) { this.action = action; }
                    public Intent(Context context, Class<?> cls) {
                        component = new ComponentName("com.voyemu", cls.getName());
                    }
                    public Intent setClassName(String pkg, String name) {
                        return setComponent(new ComponentName(pkg, name));
                    }
                    public Intent setComponent(ComponentName value) {
                        component = value;
                        return this;
                    }
                    public ComponentName getComponent() { return component; }
                    public String getAction() { return action; }
                    public Intent addFlags(int flags) { return this; }
                }
                """,
                "android/app/Service.java",
                """
                package android.app;

                public abstract class Service extends android.content.Context {
                    public static final int START_NOT_STICKY = 2;
                    public abstract android.os.IBinder onBind(android.content.Intent intent);
                    public void onCreate() {}
                    public void onDestroy() {}
                    public int onStartCommand(android.content.Intent intent, int flags, int id) {
                        return 0;
                    }
                    public void startActivity(android.content.Intent intent, android.os.Bundle options) {}
                    public void stopSelf() {}
                }
                """,
                "android/view/Display.java",
                """
                package android.view;

                public class Display {
                    public static final int DEFAULT_DISPLAY = 0;
                    private final int id;

                    public Display(int id) { this.id = id; }
                    public int getDisplayId() { return id; }
                    public String getName() { return id == 0 ? "Default" : "Emulator2D"; }
                    public boolean isValid() { return true; }
                    public Mode getMode() { return new Mode(); }

                    public static class Mode {
                        public int getPhysicalWidth() { return 1920; }
                        public int getPhysicalHeight() { return 720; }
                    }
                }
                """,
                "com/voyemu/output/ActivationPublisher.java",
                """
                package com.voyemu.output;

                final class ActivationPublisher {
                    static void publish() {}
                }
                """);
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
