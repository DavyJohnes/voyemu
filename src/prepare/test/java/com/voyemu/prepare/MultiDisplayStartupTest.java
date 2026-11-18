package com.voyemu.prepare;

import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;

import javax.tools.ToolProvider;

/** An immediately delivered native ADD exercises the actual generated onCreate ordering. */
public final class MultiDisplayStartupTest {
    public static void main(String[] args) throws Exception {
        run();
    }

    static void run() throws Exception {
        Path root =
                Path.of(System.getProperty("voyemu.project.root", System.getProperty("user.dir")))
                        .toRealPath();
        String upstream =
                Files.readString(
                        root.resolve(
                                "src/prepare/test/fixtures/multidisplay-startup/MultiDisplayService.java.txt"));
        var edits =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve(
                                                "profiles/free-09-chinese-20260106-6.0.3/components/multidisplay/upstream.edits.json"))));
        var recipe = Json.object(Json.list(edits.get("files")).get(0));
        String generated =
                SourceEdits.apply(
                        upstream, Json.list(recipe.get("edits")), Json.str(recipe, "resultSha256"));
        int begin = generated.indexOf("    @Override\n    public void onCreate() {");
        int end = generated.indexOf("\n    }\n", begin) + 7;

        StorageJsonTest.check(begin >= 0 && end > begin);
        String method = generated.substring(begin, end);
        String harness =
                """
                class StartupParent { public void onCreate() {} }
                public final class StartupRace extends StartupParent {
                  static final String TAG="fixture",DISPLAY_SERVICE="display";
                  static final int MAX_DISPLAYS=11;
                  static class System { static void loadLibrary(String ignored) {} }
                  static class Log { static void e(String tag,String value) {} }
                  static class Handler {} static class DisplayManager {} static class MultiDisplay {}
                  Handler mHandler; DisplayManager mDisplayManager; MultiDisplay[] mMultiDisplay;
                  ListenerThread mListener; boolean callbackCompleted;
                  Object getSystemService(String name) { return new DisplayManager(); }
                  final class ListenerThread {
                    void start() {
                      if(mDisplayManager==null) throw new AssertionError("ADD before DisplayManager initialization");
                      for(int id=0;id<MAX_DISPLAYS;id++)
                        if(mMultiDisplay[id]==null) throw new AssertionError("ADD before display initialization");
                      callbackCompleted=true;
                    }
                  }
                """
                        + method
                        + "\n public boolean completed() { return callbackCompleted; }\n}\n";
        Path temporary = root.resolve(".local/build/tmp");
        Storage.directory(temporary);
        Path directory = Files.createTempDirectory(temporary, "multidisplay-startup-");

        try {
            Path source = directory.resolve("StartupRace.java");
            Files.writeString(source, harness);
            var compiler = ToolProvider.getSystemJavaCompiler();
            StorageJsonTest.check(compiler != null);
            StorageJsonTest.check(
                    compiler.run(null, null, null, "-d", directory.toString(), source.toString())
                            == 0);

            try (var loader =
                    new URLClassLoader(new java.net.URL[] {directory.toUri().toURL()}, null)) {
                var type = loader.loadClass("StartupRace");
                Object service = type.getConstructor().newInstance();
                type.getMethod("onCreate").invoke(service);
                StorageJsonTest.check(
                        Boolean.TRUE.equals(type.getMethod("completed").invoke(service)));
            }
        } finally {
            FilesSupport.removeOwnedTree(directory);
        }

        java.lang.System.out.println(
                "Actual multidisplay onCreate accepts an immediate native ADD after complete"
                        + " initialization");
    }
}
