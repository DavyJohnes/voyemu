package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;

import javax.tools.ToolProvider;

/** Exercises the production source pin/edit pipeline and the guest key actually dispatched. */
public final class NxpWheelSourcesTest {
    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), work = Path.of(args[1]);
        Path component =
                args.length > 2
                        ? Path.of(args[2])
                        : root.resolve(
                                "profiles/free-09-chinese-20221230-nxp-rc45/components/wheel");
        var definition =
                Json.object(Json.parse(Files.readString(component.resolve("component.json"))));

        Files.createDirectories(work);
        ComponentSources.copy(
                Json.obj(definition, "sourceFiles"),
                work,
                resource ->
                        Files.readAllBytes(root.resolve(resource.substring("voyemu/".length()))));
        routerProbe(work, "before", true);
        Path fixtures =
                args.length > 3
                        ? Path.of(args[3])
                        : root.resolve("src/prepare/test/nxp-wheel/fixtures");
        audioProbe(work, fixtures, "before");
        editNegatives(work.resolve("negative"));
        Files.copy(
                component.resolve(Json.str(definition, "sourceEdits")),
                work.resolve(Json.str(definition, "sourceEdits")));
        ComponentSources.editDeclared(java.util.Map.of(), work);
        ComponentSources.editDeclared(definition, work);
        reject(() -> ComponentSources.editDeclared(definition, work));
        audioProbe(work, fixtures, "after");
        routerProbe(work, "after", false);
        Path input = work.resolve("src/com/voyemu/input/WheelInputSession.java");
        Path probe = work.resolve("NxpWheelProbe.java");
        Files.writeString(
                probe,
                """
                package com.voyemu.input;
                public class NxpWheelProbe {
                    public static void main(String[] args) {
                        WheelInputSession session=new WheelInputSession("nxp-test", "0123456789abcdef0123456789abcdef",0,()->100L);
                        WheelInputSession.Command command=session.prepare(1,"photo","down","test");
                        if(command.key!=142)throw new AssertionError("NXP photo must dispatch 142, not A9 173");
                        if(WheelInputSession.keyFor("volume_up")!=2||WheelInputSession.keyFor("volume_down")!=1||WheelInputSession.keyFor("ica")!=0)throw new AssertionError("NXP route drift");
                    }
                }
                """);
        Path classes = work.resolve("classes");
        Files.createDirectories(classes);

        if (ToolProvider.getSystemJavaCompiler()
                        .run(
                                null,
                                null,
                                null,
                                "--release",
                                "25",
                                "-d",
                                classes.toString(),
                                input.toString(),
                                probe.toString())
                != 0) {
            throw new AssertionError("Guest input compilation failed");
        }

        try (var loader =
                new java.net.URLClassLoader(
                        new java.net.URL[] {classes.toUri().toURL()},
                        ClassLoader.getPlatformClassLoader())) {
            loader.loadClass("com.voyemu.input.NxpWheelProbe")
                    .getMethod("main", String[].class)
                    .invoke(null, (Object) new String[0]);
        }

        System.out.println(
                "NXP production own-source adaptation and actual photo dispatch142 passed");
    }

    private interface Action {
        void run() throws Exception;
    }

    private static void reject(Action action) throws Exception {
        try {
            action.run();
        } catch (Failure error) {
            if (error.code.equals("COMPONENT_SOURCE")) {
                return;
            }

            throw error;
        }

        throw new AssertionError("Invalid own source adaptation accepted");
    }

    private static void editNegatives(Path work) throws Exception {
        Files.createDirectories(work.resolve("src"));
        Path source = work.resolve("src/Own.java"), spec = work.resolve("edits.json");
        var files = java.util.Map.<String, Object>of("src/Own.java", "declared");
        Files.writeString(source, "class Own { int key=173; }\n");
        String valid =
                "{\"version\":1,\"files\":[{\"path\":\"src/Own.java\",\"replacements\":[{\"before\":\"key=173\",\"after\":\"key=142\"}]}]}";
        Files.writeString(spec, valid.replace("src/Own.java", "src/Foreign.java"));
        reject(() -> ComponentSources.edit(files, work, spec));
        Files.writeString(spec, valid.replace("key=173", "missing"));
        reject(() -> ComponentSources.edit(files, work, spec));
        Files.writeString(source, "class Own { int key=173; int other=173; }\n");
        Files.writeString(spec, valid.replace("key=173", "173"));
        reject(() -> ComponentSources.edit(files, work, spec));
        Files.writeString(source, "aaaa");
        Files.writeString(spec, valid.replace("key=173", "aaa"));
        reject(() -> ComponentSources.edit(files, work, spec));
        Files.writeString(
                source, "// unrelated own source formatting\nclass Own { int key=173; }\n");
        Files.writeString(spec, valid);
        ComponentSources.edit(files, work, spec);
        reject(() -> ComponentSources.edit(files, work, spec));
        Files.writeString(
                spec,
                valid.replace(
                        "\"replacements\"", "\"resultSha256\":\"forbidden\",\"replacements\""));
        reject(() -> ComponentSources.edit(files, work, spec));

        try {
            SourceEdits.apply("external", java.util.List.of(), "0".repeat(64));
        } catch (Failure error) {
            if (error.code.equals("SOURCE_EDIT_HASH_MISMATCH")) {
                return;
            }

            throw error;
        }

        throw new AssertionError("External source output hash guard removed");
    }

    private static void audioProbe(Path work, Path fixtures, String stage) throws Exception {
        Path classes = work.resolve("audio-" + stage);

        Files.createDirectories(classes);
        var arguments =
                new java.util.ArrayList<String>(
                        java.util.List.of(
                                "--release",
                                "25",
                                "-d",
                                classes.toString(),
                                work.resolve("src/com/voyemu/input/AudioControl.java").toString()));

        try (var sources = Files.walk(fixtures)) {
            sources.filter(path -> path.toString().endsWith(".java"))
                    .sorted()
                    .forEach(path -> arguments.add(path.toString()));
        }

        if (ToolProvider.getSystemJavaCompiler()
                        .run(null, null, null, arguments.toArray(String[]::new))
                != 0) {
            throw new AssertionError("Materialized NXP audio source compilation failed");
        }

        try (var loader =
                new java.net.URLClassLoader(
                        new java.net.URL[] {classes.toUri().toURL()},
                        ClassLoader.getPlatformClassLoader())) {
            loader.loadClass("com.voyemu.input.NxpWheelAudioProbe")
                    .getMethod("main", String[].class)
                    .invoke(null, (Object) new String[] {stage});
        }

        System.out.println(
                stage.equals("before")
                        ? "RED: A9 reflection ABI rejected by NXP audio fixture"
                        : "GREEN: NXP audio current/fallback policy, volume/readback and wrong-ABI"
                                + " rejection");
    }

    private static void routerProbe(Path work, String stage, boolean expectFailure)
            throws Exception {
        Path probeRoot = work.resolve(stage), classes = probeRoot.resolve("classes");

        Files.createDirectories(classes);
        Path audio = probeRoot.resolve("AudioControl.java"),
                probe = probeRoot.resolve("NxpRouterProbe.java");
        Files.writeString(
                audio,
                "package com.voyemu.input; public final class AudioControl { public static final"
                        + " class VolumeChange {} }\n");
        Files.writeString(
                probe,
                """
                package com.voyemu.input;
                public final class NxpRouterProbe {
                    public static void main(String[] args) {
                        KeyEventRouter router=new KeyEventRouter();
                        int[] counts=new int[4];
                        int[] keys={6,3,4};
                        router.register("com.qinggan.app.music",0,50,keys,(code,action,pkg,type)->{counts[0]++;return action==WheelInputSession.SHORT;});
                        router.register("com.qinggan.app.music",1,50,keys,(code,action,pkg,type)->{counts[1]++;return false;});
                        router.register("com.qinggan.media",0,50,keys,(code,action,pkg,type)->{counts[2]++;return false;});
                        router.register("com.example.other",0,50,keys,(code,action,pkg,type)->{counts[3]++;return false;});
                        for(int code:keys)for(int action:new int[]{WheelInputSession.DOWN,WheelInputSession.SHORT,WheelInputSession.UP}){
                            int before=counts[0];
                            KeyEventRouter.Delivery delivery=router.dispatch(code,action);
                            if(counts[0]!=before+1)throw new AssertionError("NXP normal media callback missing");
                            if(delivery.callbacks!=2||delivery.consumed!=(action==WheelInputSession.SHORT))throw new AssertionError("NXP phase delivery differs");
                            boolean normal=false;
                            for(var receipt:delivery.callbackReceipts)if(receipt.get("registration").equals("com.qinggan.app.music")&&receipt.get("type").equals(0)&&receipt.get("key_code").equals(code)&&receipt.get("action").equals(action))normal=true;
                            if(!normal)throw new AssertionError("NXP normal receipt missing");
                        }
                        if(counts[0]!=9||counts[1]!=9||counts[2]!=0||counts[3]!=0)throw new AssertionError("Wrong media owner callback");
                        if(router.dispatch(130,WheelInputSession.SHORT).callbacks!=0)throw new AssertionError("Unregistered key delivered");
                    }
                }
                """);

        if (ToolProvider.getSystemJavaCompiler()
                        .run(
                                null,
                                null,
                                null,
                                "--release",
                                "25",
                                "-d",
                                classes.toString(),
                                work.resolve("src/com/voyemu/input/KeyEventRouter.java").toString(),
                                work.resolve("src/com/voyemu/input/WheelInputSession.java")
                                        .toString(),
                                audio.toString(),
                                probe.toString())
                != 0) {
            throw new AssertionError("Actual router compilation failed");
        }

        boolean red = false;

        try (var loader =
                new java.net.URLClassLoader(
                        new java.net.URL[] {classes.toUri().toURL()},
                        ClassLoader.getPlatformClassLoader())) {
            try {
                loader.loadClass("com.voyemu.input.NxpRouterProbe")
                        .getMethod("main", String[].class)
                        .invoke(null, (Object) new String[0]);
            } catch (java.lang.reflect.InvocationTargetException error) {
                if (expectFailure
                        && error.getCause() instanceof AssertionError
                        && "NXP normal media callback missing"
                                .equals(error.getCause().getMessage())) {
                    red = true;
                } else {
                    throw error;
                }
            }
        }

        if (expectFailure && !red) {
            throw new AssertionError(
                    "Original A9 router did not reproduce the missing NXP callback");
        }

        System.out.println(
                expectFailure
                        ? "RED: A9 owner omits actual NXP normal media callback"
                        : "GREEN: materialized NXP router dispatches toggle/next/previous phases"
                                + " once and rejects foreign owners");
    }
}
