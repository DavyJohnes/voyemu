package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * The Scenes, Drive recorder and Photos launcher tiles get their owner APKs with framework
 * libraries.
 */
public final class PassionLauncherAppsRecipeTest {
    record App(String role, String pkg, String sha256, List<String> steps, String target) {}

    static final List<App> APPS =
            List.of(
                    new App(
                            "VOYAH_SceneMode",
                            "com.voyah.scenemode",
                            "65c0f11403d89ae104349f59bbab73e461735fe9c93ede6077842f0cc7425f22",
                            List.of("framework-libraries.json"),
                            "/system/app/VOYAH_SceneMode/VOYAH_SceneMode.apk"),
                    new App(
                            "MonzaDVR",
                            "com.mega.dvr",
                            "09f3c93858cd49ab65884d115787322149e4d2a5861cb7cf314f1f6726f05294",
                            List.of("passion-api30-media-libraries.json"),
                            "/system/priv-app/MonzaDVR/MonzaDVR.apk"),
                    new App(
                            "LY_Gallery",
                            "com.voyah.gallery",
                            "27b3c2323a6e2f230e99608268b694be2c30e143d806bc671dfb0702b51a7689",
                            List.of("passion-api30-media-libraries.json"),
                            "/system/app/LY_Gallery/LY_Gallery.apk"));
    static final List<String> LIBRARIES =
            List.of("android.car", "voyemu.platform.callbacks", "voyemu.window", "voyemu.mega");

    static final class Target implements Recipes.Target {
        final String pin;
        final List<List<String>> libraries = new ArrayList<>();

        Target(String pin) {
            this.pin = pin;
        }

        @Override
        public int api() {
            return 30;
        }

        @Override
        public String abi() {
            return "arm64-v8a";
        }

        @Override
        public Path work() {
            return null;
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
        public String originalSha256() {
            return pin;
        }

        @Override
        public void addDex(String value) {
            throw new AssertionError();
        }

        @Override
        public void putEntry(Path path, String name) {
            throw new AssertionError();
        }

        @Override
        public void removeEntry(String name) {
            throw new AssertionError();
        }

        @Override
        public void useLibraries(List<String> names) {
            libraries.add(names);
        }
    }

    static void check(boolean ok, String reason) {
        if (!ok) {
            throw new AssertionError(reason);
        }
    }

    static Map<String, Object> read(Path file) throws Exception {
        return Json.object(Json.parse(Files.readString(file)));
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]);

        for (int i = 0; i < APPS.size(); i++) {
            App app = APPS.get(i);
            Path profile = root.resolve("profiles").resolve(TestProfiles.pinning(root, app.sha256));
            var data = read(profile.resolve("profile.json"));
            var original = Json.obj(Json.obj(data, "original"), app.role);
            check(
                    app.sha256.equals(Json.str(original, "sha256"))
                            && app.pkg.equals(Json.str(Json.obj(original, "source"), "package"))
                            && Json.str(original, "file").equals(app.role + ".apk"),
                    "Owner APK of " + app.role + " is not pinned");
            check(
                    Json.obj(Json.obj(data, "patches"), app.role)
                            .equals(
                                    Map.of(
                                            "input",
                                            "original:" + app.role,
                                            "type",
                                            "apk",
                                            "steps",
                                            app.steps)),
                    "Exact library chain of " + app.role + " required");
            long installs =
                    Json.list(data.get("install")).stream()
                            .filter(item ->
                                    Json.object(item)
                                            .equals(
                                                    Map.of(
                                                            "artifact",
                                                            "patched:" + app.role,
                                                            "type",
                                                            "file",
                                                            "target",
                                                            app.target,
                                                            "mode",
                                                            "0644")))
                            .count();
            check(installs == 1, "One installed APK of " + app.role + " required");
            var target = new Target(app.sha256);

            for (String step : app.steps) {
                var recipe = read(profile.resolve("patches").resolve(step));
                recipe.put("sourceSha256", "a".repeat(64));
                Recipes.apply(step, recipe, target);
                var foreign = new Target("0".repeat(64));
                boolean denied = false;

                try {
                    Recipes.apply(step, recipe, foreign);
                } catch (Failure expected) {
                    denied = true;
                }

                check(denied && foreign.libraries.isEmpty(), "Foreign APK accepted by " + step);
            }

            check(
                    target.libraries.equals(List.of(LIBRARIES)),
                    app.role + " must declare the framework libraries once");

            if (args.length == APPS.size() + 1) {
                Path apk = Path.of(args[i + 1]);
                check(NativeCore.hash(Files.readAllBytes(apk)).equals(app.sha256), "Owner APK");
                byte[] manifest = NativeCore.entries(apk).get("AndroidManifest.xml");
                byte[] patched = ManifestLibraries.add(manifest, LIBRARIES);
                boolean repeated = false;

                try {
                    ManifestLibraries.add(patched, LIBRARIES);
                } catch (Failure expected) {
                    repeated = true;
                }

                check(repeated, app.role + " manifest must declare the libraries");
            }
        }

        System.out.println(
                "Passion launcher apps PASS: Scenes, Drive recorder and Photos owner chains");
    }
}
