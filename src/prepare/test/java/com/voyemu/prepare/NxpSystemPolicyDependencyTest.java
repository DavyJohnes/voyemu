package com.voyemu.prepare;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

/** A system shared-UID APK must use the image platform signer while retaining OEM contents. */
public final class NxpSystemPolicyDependencyTest {
    static void need(boolean value, String reason) {
        if (!value) {
            throw new AssertionError(reason);
        }
    }

    static void recipe(Map<String, Object> profile) {
        var patch = Json.obj(Json.obj(profile, "patches"), "SystemPolicy");

        need(
                "apk".equals(patch.get("type"))
                        && "original:SystemPolicy".equals(patch.get("input")),
                "OEM SystemPolicy input required");
        need("platform".equals(patch.get("sign")), "System shared UID requires platform signer");
        need(
                Json.list(patch.get("steps"))
                        .equals(
                                List.of(
                                        "nxp-systempolicy-hardware.json",
                                        "qgapi-use-libraries.json",
                                        "add-qg-runtime.json")),
                "SystemPolicy requires QGAPI classloader and native runtime");
        need(
                Json.list(patch.get("uses")).contains("component:qg-runtime"),
                "Native runtime dependency missing");
        int count = 0;

        for (Object value : Json.list(profile.get("install"))) {
            var item = Json.object(value);

            if (!"com.qinggan.systempolicy".equals(item.get("package"))) {
                continue;
            }

            count++;
            need(
                    "patched:SystemPolicy".equals(item.get("artifact")),
                    "Unsigned original cannot enter system shared UID");
            need(
                    "/system/priv-app/SystemPolicy-release/SystemPolicy-release.apk"
                            .equals(item.get("target")),
                    "OEM privileged target required");
        }

        need(count == 1, "Exactly one SystemPolicy install required");
    }

    static void reject(Map<String, Object> profile) {
        boolean rejected = false;

        try {
            recipe(profile);
        } catch (AssertionError expected) {
            rejected = true;
        }

        need(rejected, "Broken shared UID delivery accepted");
    }

    static Map<String, Object> copy(Map<String, Object> profile) {
        return Json.object(Json.parse(Json.write(profile)));
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]);
        String id = "free-09-chinese-20221230-nxp-rc45";
        var profile =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve("profiles/" + id + "/profile.json"))));

        recipe(profile);
        Path apk = root.resolve(".local/profiles/" + id + "/originals/systempolicy.apk");
        NativeCore.pinned(apk, "c4920cc36a257755335c6fae098172e30414711e9f2adf3fab6dbc566edabd15");
        Path aapt =
                root.resolve(
                        ".local/toolchains/native-sdk/build-tools/36.1.0/aapt2"
                                + (System.getProperty("os.name").startsWith("Windows")
                                        ? ".exe"
                                        : ""));
        var process =
                new ProcessBuilder(
                                aapt.toString(),
                                "dump",
                                "xmltree",
                                apk.toString(),
                                "--file",
                                "AndroidManifest.xml")
                        .redirectErrorStream(true)
                        .start();
        String xml =
                new String(
                        process.getInputStream().readAllBytes(),
                        java.nio.charset.StandardCharsets.UTF_8);
        need(process.waitFor() == 0, "OEM manifest decode failed");
        need(
                xml.contains("sharedUserId(0x0101000b)=\"android.uid.system\""),
                "OEM shared UID changed");
        need(
                xml.contains("package=\"com.qinggan.systempolicy\"")
                        && xml.contains("com.qinggan.systempolicy.SystemPolicyService")
                        && xml.contains("com.qinggan.systempolicy.SystemPolicyApplication"),
                "OEM package/service/application contract changed");
        var container = DexFileFactory.loadDexContainer(apk.toFile(), Opcodes.forApi(28));
        boolean needsQgapi = false, localDefinition = false;

        for (String entry : container.getDexEntryNames()) {
            for (var type : container.getEntry(entry).getDexFile().getClasses()) {
                if (type.getType().equals("Lcom/qinggan/systempolicy/SystemPolicyApplication;")) {
                    needsQgapi = "Lcom/qinggan/app/QGApplication;".equals(type.getSuperclass());
                }

                if (type.getType().equals("Lcom/qinggan/app/QGApplication;")) {
                    localDefinition = true;
                }
            }
        }

        need(
                needsQgapi && !localDefinition,
                "Actual OEM application must expose missing external QGApplication superclass");
        var donor =
                Json.obj(
                        Json.object(
                                Json.parse(
                                        Files.readString(
                                                root.resolve(
                                                        "profiles/"
                                                                + id
                                                                + "/components/framework-qgapi/component.json")))),
                        "donorDex");
        Path donorApk = root.resolve(".local/profiles/" + id + "/originals/qgime.apk");
        NativeCore.pinned(donorApk, Json.str(donor, "sha256"));
        var donorDex = DexFileFactory.loadDexContainer(donorApk.toFile(), Opcodes.forApi(28));
        boolean supplied = false;

        for (String entry : donorDex.getDexEntryNames()) {
            for (var type : donorDex.getEntry(entry).getDexFile().getClasses()) {
                if (type.getType().equals("Lcom/qinggan/app/QGApplication;")) {
                    supplied = true;
                }
            }
        }

        need(supplied, "Pinned OEM donor must define QGApplication");
        need(
                Files.readString(
                                root.resolve(
                                        "profiles/"
                                                + id
                                                + "/components/framework-qgapi/component.json"))
                        .contains("\"com.qinggan.app.QGApplication\""),
                "QGAPI donor must provide actual external superclass");

        try (var zip = new java.util.zip.ZipFile(apk.toFile())) {
            byte[] original =
                    zip.getInputStream(zip.getEntry("AndroidManifest.xml")).readAllBytes();
            byte[] patched = ManifestLibraries.add(original, List.of("QGAPI"));
            Path checked =
                    Files.createTempFile(
                            root.resolve(".local/build/jobs"), "systempolicy-qgapi-", ".apk");

            try {
                NativeCore.zip(checked, Map.of("AndroidManifest.xml", patched), true);
                var inspect =
                        new ProcessBuilder(
                                        aapt.toString(),
                                        "dump",
                                        "xmltree",
                                        checked.toString(),
                                        "--file",
                                        "AndroidManifest.xml")
                                .redirectErrorStream(true)
                                .start();
                String text =
                        new String(
                                inspect.getInputStream().readAllBytes(),
                                java.nio.charset.StandardCharsets.UTF_8);
                need(
                        inspect.waitFor() == 0
                                && text.contains("QGAPI")
                                && text.contains("uses-library")
                                && text.contains("android.uid.system")
                                && text.contains("com.qinggan.systempolicy.SystemPolicyService"),
                        "Actual binary manifest library patch must preserve system/service"
                                + " contract");
            } finally {
                Files.delete(checked);
            }
        }

        var bad = copy(profile);
        var patches = Json.obj(bad, "patches");
        var patch = Json.obj(patches, "SystemPolicy");
        patch.put("sign", "oem");
        patches.put("SystemPolicy", patch);
        bad.put("patches", patches);
        reject(bad);
        bad = copy(profile);
        var installs = Json.list(bad.get("install"));

        for (int i = 0; i < installs.size(); i++) {
            var item = Json.object(installs.get(i));

            if ("com.qinggan.systempolicy".equals(item.get("package"))) {
                item.put("artifact", "original:SystemPolicy");
                installs.set(i, item);
            }
        }

        bad.put("install", installs);
        reject(bad);
        bad = copy(profile);
        patches = Json.obj(bad, "patches");
        patch = Json.obj(patches, "SystemPolicy");
        patch.put("steps", List.of("add-qg-runtime.json"));
        patches.put("SystemPolicy", patch);
        bad.put("patches", patches);
        reject(bad);
        System.out.println(
                "SystemPolicy pinned OEM manifest/shared UID/service and platform delivery PASS");
    }
}
