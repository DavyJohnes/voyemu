package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipFile;

/** Verifies actual OEM native replacement, with rejection before mutation. */
public final class NxpVoiceNativeTest {
    static final String ID = "free-09-chinese-20221230-nxp-rc45",
            NAME = "nxp-voice-arm64-natives.json",
            PIN = "619186d34262ea53e5386abb52ca11d74636da2a85434fafa19df7efecca565b";

    static void need(boolean v) {
        if (!v) {
            throw new AssertionError();
        }
    }

    static final class Target implements Recipes.Target {
        String id = ID, pin = PIN, abi = "arm64-v8a";
        int api = 28;
        Map<String, Path> uses = new LinkedHashMap<>();
        Map<String, byte[]> entries = new LinkedHashMap<>();
        int mutations;

        @Override
        public String profile() {
            return id;
        }

        @Override
        public String originalSha256() {
            return pin;
        }

        @Override
        public int api() {
            return api;
        }

        @Override
        public String abi() {
            return abi;
        }

        @Override
        public Path work() {
            throw new AssertionError();
        }

        @Override
        public Path output() {
            throw new AssertionError();
        }

        @Override
        public Map<String, Path> uses() {
            return uses;
        }

        @Override
        public void verifyPinnedEntry(String n, String p) throws Exception {
            Artifacts.verifyPinnedNativeEntry(entries, n, p);
        }

        @Override
        public void removeEntry(String n) {
            need(entries.remove(n) != null);
            mutations++;
        }

        @Override
        public void putEntry(Path p, String n) throws Exception {
            byte[] b = Files.readAllBytes(p);

            need(
                    b[0] == 127
                            && b[1] == 69
                            && b[2] == 76
                            && b[3] == 70
                            && b[4] == 2
                            && b[18] == (byte) 183
                            && b[19] == 0);
            entries.put(n, b);
            mutations++;
        }

        @Override
        public void addDex(String s) {
            throw new AssertionError();
        }

        @Override
        public void useLibraries(List<String> n) {
            throw new AssertionError();
        }
    }

    static void reject(Target t, Map<String, Object> recipe) throws Exception {
        int before = t.mutations;
        boolean rejected = false;

        try {
            Recipes.apply(NAME, recipe, t);
        } catch (Failure expected) {
            rejected = true;
        }

        need(rejected && before == t.mutations);
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), inputs = Path.of(args[1]);
        Target t = new Target();
        Path recipeFile =
                args.length > 2
                        ? Path.of(args[2])
                        : root.resolve("profiles/" + ID + "/patches/" + NAME);
        var recipe = Json.object(Json.parse(Files.readString(recipeFile)));

        recipe.put("sourceSha256", FilesSupport.hash(recipeFile));
        Path apk = inputs.resolve("wakeupspeechservice.apk").toRealPath();
        NativeCore.pinned(apk, PIN);

        try (var z = new ZipFile(apk.toFile())) {
            t.entries.put(
                    "lib/armeabi/libSpeechRecord.so",
                    z.getInputStream(z.getEntry("lib/armeabi/libSpeechRecord.so")).readAllBytes());
        }

        t.uses.put("component:qg-runtime", root.resolve("unused-qg-runtime"));
        t.uses.put(
                "original:voice-libduidns.so", inputs.resolve("system-libduidns.so").toRealPath());
        t.uses.put(
                "original:voice-libsemantic.so",
                inputs.resolve("system-libsemantic.so").toRealPath());
        t.uses.put(
                "original:voice-libvprint.so", inputs.resolve("system-libvprint.so").toRealPath());
        t.uses.put(
                "original:voice-libfespcar.so",
                inputs.resolve("system-libfespcar.so").toRealPath());
        t.uses.put(
                "original:voice-libduiutils.so",
                inputs.resolve("system-libduiutils.so").toRealPath());
        t.uses.put("original:voice-liblame.so", inputs.resolve("system-liblame.so").toRealPath());
        t.uses.put("original:voice-libdmasp.so", inputs.resolve("system-libdmasp.so").toRealPath());
        t.uses.put("original:voice-libvad.so", inputs.resolve("system-libvad.so").toRealPath());
        t.uses.put("original:voice-libecho.so", inputs.resolve("system-libecho.so").toRealPath());
        t.uses.put("original:voice-libasr.so", inputs.resolve("system-libasr.so").toRealPath());
        t.uses.put("original:voice-libgram.so", inputs.resolve("system-libgram.so").toRealPath());
        t.uses.put("original:voice-libasrpp.so", inputs.resolve("system-libasrpp.so").toRealPath());
        t.uses.put(
                "original:voice-libwakeup.so", inputs.resolve("system-libwakeup.so").toRealPath());
        t.uses.put("original:voice-libcntts.so", inputs.resolve("system-libcntts.so").toRealPath());
        t.uses.put(
                "original:voice-libliteca.so", inputs.resolve("system-libliteca.so").toRealPath());
        t.uses.put(
                "original:voice-libSpeechRecord.so",
                inputs.resolve("system-libSpeechRecord.so").toRealPath());
        t.uses.put(
                "original:voice-libSpeechRecord4Mic.so",
                inputs.resolve("system-libSpeechRecord4Mic.so").toRealPath());
        t.uses.put(
                "original:voice-libqgspeechagc.so",
                inputs.resolve("system-libqgspeechagc.so").toRealPath());
        t.uses.put("original:voice-libqgvad.so", inputs.resolve("system-libqgvad.so").toRealPath());
        t.uses.put(
                "original:voice-libvoiceengnie.so",
                inputs.resolve("system-libvoiceengnie.so").toRealPath());
        t.uses.put(
                "original:voice-libdasock.so", inputs.resolve("system-libdasock.so").toRealPath());
        t.id = "foreign";
        reject(t, recipe);
        t.id = ID;
        t.pin = "0".repeat(64);
        reject(t, recipe);
        t.pin = PIN;
        t.api = 30;
        reject(t, recipe);
        t.api = 28;
        t.abi = "armeabi-v7a";
        reject(t, recipe);
        t.abi = "arm64-v8a";
        var wrong = new LinkedHashMap<String, Object>(recipe);
        wrong.put("constants", Map.of("INPUT_SHA256", "0".repeat(64)));
        reject(t, wrong);
        Path donor = t.uses.remove("original:voice-libwakeup.so");
        reject(t, recipe);
        t.uses.put("original:voice-libwakeup.so", donor);
        Recipes.apply(NAME, recipe, t);
        need(!t.entries.containsKey("lib/armeabi/libSpeechRecord.so") && t.entries.size() == 21);

        for (var e : t.uses.entrySet()) {
            if (e.getKey().startsWith("original:")) {
                String name = e.getKey().substring("original:voice-".length());
                need(
                        Arrays.equals(
                                Files.readAllBytes(e.getValue()),
                                t.entries.get("lib/arm64-v8a/" + name)));
            }
        }

        System.out.println(
                "NXP Voice native PASS: actual APK ARM32 replacement; exact ARM64 OEM bytes;"
                    + " profile/APK/API/ABI/recipe/dependency/native-entry negative guards before"
                    + " mutation");
    }
}
