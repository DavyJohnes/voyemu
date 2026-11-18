package com.voyemu.prepare;

import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import javax.tools.ToolProvider;

/**
 * Exercises the actual own publisher through recording Android/QGAPI boundaries; no guest readiness
 * claim.
 */
public final class ActivationPublisherTest {
    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static final class Fixture implements AutoCloseable {
        final URLClassLoader loader;
        final Class<?> state, publisher;

        Fixture(Path classes) throws Exception {
            loader = new URLClassLoader(new java.net.URL[] {classes.toUri().toURL()}, null);
            state = loader.loadClass("android.os.Fixture");
            publisher = loader.loadClass("com.voyemu.output.ActivationPublisher");
        }

        void set(String name, Object value) throws Exception {
            state.getField(name).set(null, value);
        }

        Object get(String name) throws Exception {
            return state.getField(name).get(null);
        }

        int count(String name) throws Exception {
            return (Integer) get(name);
        }

        void publish() throws Exception {
            var method = publisher.getDeclaredMethod("publish");

            method.setAccessible(true);
            method.invoke(null);
        }

        void flush() throws Exception {
            state.getMethod("flush").invoke(null);
        }

        void noWrite() throws Exception {
            check(
                    count("sets") == 0 && count("pokes") == 0,
                    "Ineligible/preflight failure must never write or poke");
        }

        @Override
        public void close() throws Exception {
            loader.close();
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            throw new IllegalArgumentException("ROOT");
        }

        Path root = Path.of(args[0]).toRealPath();
        String profile = "free-11-chinese-20260731-7.3.1";
        Path component = root.resolve("profiles/" + profile + "/components/bootstrap"),
                source = component.resolve("src/com/voyemu/output/ActivationPublisher.java");
        Path oldSeed = root.resolve("profiles/" + profile + "/files/nvram.txt"),
                activeSeed = root.resolve("profiles/" + profile + "/files/nvram-active.txt");
        Path work =
                Files.createTempDirectory(
                        root.resolve(".local/build/tmp"), "qinggan11-731-activation-test-");

        try {
            Map<String, String> files = new LinkedHashMap<>();
            files.put(
                    "android/os/Fixture.java",
                    """
                    package android.os; import java.util.*;
                    public final class Fixture {
                        public static String value, file, property=""; public static boolean readFail,setFail,binderFail,binderFalse,binderMissing,scheduleFail,mismatch,callback;
                        public static int failGetAt,gets,sets,pokes,obtained,recycled,notifications,consumerStarts,scheduled;
                        public static java.util.List<Integer> keys=new ArrayList<>(); public static java.util.List<String> events=new ArrayList<>(),logs=new ArrayList<>();public static java.util.List<Runnable> queue=new ArrayList<>();
                        public static void flush(){while(!queue.isEmpty())queue.remove(0).run();}
                    }
                    """);
            files.put(
                    "com/qinggan/os/SystemProperties.java",
                    """
                    package com.qinggan.os; import android.os.Fixture; public final class SystemProperties {
                        public static String nvramRead(int key)throws Exception{Fixture.keys.add(key);Fixture.events.add("nvram:"+key);if(Fixture.readFail)throw new IllegalStateException("unavailable");if(Fixture.file!=null){for(String line:java.nio.file.Files.readAllLines(java.nio.file.Path.of(Fixture.file)))if(line.startsWith(key+"="))return line.substring((key+"=").length());return "";}return Fixture.value;}
                    }
                    """);
            files.put(
                    "android/os/SystemProperties.java",
                    """
                    package android.os; public final class SystemProperties {
                        public static String get(String key,String fallback){if(!key.equals("ignoreActivation")||!fallback.equals(""))throw new AssertionError();Fixture.events.add("get");if(++Fixture.gets==Fixture.failGetAt)throw new IllegalStateException("get unavailable");return Fixture.mismatch&&Fixture.gets>1?"false":Fixture.property;}
                        public static void set(String key,String value){if(!key.equals("ignoreActivation")||!value.equals("true"))throw new AssertionError();Fixture.sets++;Fixture.events.add("set");Fixture.property=value;if(Fixture.setFail)throw new IllegalStateException("write may have happened");}
                    }
                    """);
            files.put(
                    "android/os/AsyncTask.java",
                    "package android.os; public final class AsyncTask {public static void"
                        + " execute(Runnable task){if(Fixture.scheduleFail)throw new"
                        + " IllegalStateException();Fixture.scheduled++;Fixture.queue.add(task);}}");
            files.put(
                    "android/os/Parcel.java",
                    "package android.os; public final class Parcel {public boolean used; public"
                        + " static Parcel obtain(){Fixture.obtained++;return new Parcel();}public"
                        + " void recycle(){if(used)throw new"
                        + " AssertionError();used=true;Fixture.recycled++;}}");
            files.put(
                    "android/os/IBinder.java",
                    "package android.os; public interface IBinder {boolean transact(int code,Parcel"
                            + " data,Parcel reply,int flags)throws Exception;}");
            files.put(
                    "android/os/ServiceManager.java",
                    """
                    package android.os; public final class ServiceManager {
                        public static IBinder checkService(String name){if(!name.equals("activity"))throw new AssertionError("Only original activity target allowed");Fixture.events.add("activity");if(Fixture.binderMissing)return null;return new IBinder(){public boolean transact(int code,Parcel data,Parcel reply,int flags)throws Exception{if(code!=1599295570||data==null||data.used||reply!=null||flags!=0)throw new AssertionError("Original emptyParcel/nullreply/flags0 contract");Fixture.pokes++;Fixture.events.add("poke");if(Fixture.binderFail)throw new IllegalStateException("outcome unknown");if(Fixture.binderFalse)return false;Fixture.notifications++;if(Fixture.callback&&Fixture.property.equals("true")){Fixture.consumerStarts++;Fixture.callback=false;}return true;}};}
                    }
                    """);
            files.put(
                    "android/util/Log.java",
                    "package android.util; public final class Log {public static int i(String"
                        + " tag,String value){android.os.Fixture.logs.add(value);return 0;}public"
                        + " static int w(String tag,String value,Throwable"
                        + " cause){android.os.Fixture.logs.add(value);return 0;}}");
            List<String> compiler =
                    new ArrayList<>(
                            List.of("--release", "25", "-d", work.resolve("classes").toString()));

            for (var entry : files.entrySet()) {
                Path path = work.resolve(entry.getKey());
                Files.createDirectories(path.getParent());
                Files.writeString(path, entry.getValue());
                compiler.add(path.toString());
            }

            compiler.add(source.toString());
            check(
                    ToolProvider.getSystemJavaCompiler()
                                    .run(null, null, null, compiler.toArray(String[]::new))
                            == 0,
                    "Actual publisher compiles against recording original boundaries");
            Path classes = work.resolve("classes");
            checkSeedScenarios(classes, oldSeed, activeSeed);
            checkFailureScenarios(classes);
        } finally {
            FilesSupport.removeOwnedTree(work);
        }

        System.out.println(
                "Actual publisher: ten false/unknown truth cases, real old-seed RED/active-seed"
                    + " GREEN, async singleton, cold/readback and preexisting callback model, four"
                    + " preflight failures, five unknown post-write no-replay failures, exact"
                    + " activity/Parcel contract PASS; guest/Hiboard readiness unverified");
    }

    private static void checkSeedScenarios(Path classes, Path oldSeed, Path activeSeed)
            throws Exception {
        for (String value :
                new String[] {
                    null, "", "false", "1", "TRUE", "True", " true", "true ", "true\n", "unknown"
                }) {
            try (var fixture = new Fixture(classes)) {
                fixture.set("value", value);
                fixture.publish();
                fixture.flush();
                fixture.noWrite();
                check(fixture.get("keys").equals(List.of(8)), "Only original NVRAMslot8 read");
            }
        }

        for (Path seed : List.of(oldSeed, activeSeed)) {
            try (var fixture = new Fixture(classes)) {
                fixture.set("file", seed.toString());
                fixture.publish();
                fixture.publish();
                check(fixture.count("scheduled") == 1, "Only one async publication in flight");
                fixture.flush();

                if (seed.equals(oldSeed)) {
                    fixture.noWrite();
                } else {
                    check(
                            fixture.count("sets") == 1
                                    && fixture.count("pokes") == 1
                                    && fixture.count("obtained") == 1
                                    && fixture.count("recycled") == 1,
                            "Real own active fixture publishes and recycles exact"
                                    + " notification");
                    check(
                            fixture.get("events")
                                    .equals(
                                            List.of(
                                                    "nvram:8",
                                                    "get",
                                                    "activity",
                                                    "set",
                                                    "get",
                                                    "poke")),
                            "Original state read, full preflight, write, readback, then"
                                    + " activity-only poke");
                    check(
                            fixture.get("property").equals("true"),
                            "Cold later consumer can read actual published property");
                    fixture.publish();
                    fixture.flush();
                    check(
                            fixture.count("sets") == 1 && fixture.count("pokes") == 1,
                            "Repeated service start cannot replay successful publication");
                }
            }
        }
    }

    private static void checkFailureScenarios(Path classes) throws Exception {
        try (var fixture = new Fixture(classes)) {
            fixture.set("value", "true");
            fixture.set("callback", true);
            fixture.publish();
            fixture.flush();
            check(
                    fixture.count("consumerStarts") == 1
                            && Boolean.FALSE.equals(fixture.get("callback")),
                    "Preexisting false consumer gets one modeled original property callback");
            fixture.publish();
            fixture.flush();
            check(
                    fixture.count("consumerStarts") == 1,
                    "Repeated true cannot synthesize another panel start");
        }

        for (String failure : List.of("readFail", "binderMissing", "scheduleFail", "baselineGet")) {
            try (var fixture = new Fixture(classes)) {
                fixture.set("value", "true");

                if (failure.equals("baselineGet")) {
                    fixture.set("failGetAt", 1);
                } else {
                    fixture.set(failure, true);
                }

                fixture.publish();
                fixture.flush();
                fixture.noWrite();
                check(
                        fixture.count("obtained") == fixture.count("recycled"),
                        "Preflight failure has no leaked Parcel");
            }
        }

        for (String failure :
                List.of("setFail", "mismatch", "postGet", "binderFail", "binderFalse")) {
            try (var fixture = new Fixture(classes)) {
                fixture.set("value", "true");

                if (failure.equals("postGet")) {
                    fixture.set("failGetAt", 2);
                } else {
                    fixture.set(failure, true);
                }

                fixture.publish();
                fixture.flush();
                check(
                        fixture.count("sets") == 1
                                && fixture.count("obtained") == 1
                                && fixture.count("recycled") == 1,
                        "Uncertain mutation retains honest attempted boundary and releases"
                                + " Parcel");
                check(
                        ((List<?>) fixture.get("logs"))
                                .stream()
                                        .anyMatch(x ->
                                                x.toString()
                                                        .contains(
                                                                "result unknown after"
                                                                        + " write"
                                                                        + " attempt")),
                        "Post-write failure must report unknown, not claim no write");
                fixture.publish();
                fixture.flush();
                check(
                        fixture.count("sets") == 1 && fixture.count("scheduled") == 1,
                        "Uncertain write/poke must never replay on duplicate start");
            }
        }
    }
}
