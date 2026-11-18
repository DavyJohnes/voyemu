package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;

public final class NxpVoiceInputsTest {
    static void need(boolean v, String m) {
        if (!v) {
            throw new AssertionError(m);
        }
    }

    static void voiceWiring(Map<String, Object> p) {
        var patches = Json.obj(p, "patches");
        var setting = Json.obj(patches, "Setting");

        need(
                Json.str(setting, "input").equals("original:Setting"),
                "Voice recipe requires the actual Settings APK");
        need(
                Json.str(Json.obj(Json.obj(p, "original"), "Setting"), "sha256")
                        .equals(NxpVoiceInitializationTest.PIN),
                "Voice recipe Settings input pin differs");
        int uses = 0;

        for (var node : patches.entrySet()) {
            for (var step : Json.list(Json.object(node.getValue()).get("steps"))) {
                if (step.equals(NxpVoiceInitializationTest.NAME)) {
                    need(
                            node.getKey().equals("Setting"),
                            "Settings constructor recipe attached to another APK");
                    uses++;
                }
            }
        }

        need(uses == 1, "Settings constructor recipe must appear exactly once on Settings");
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), inputs = Path.of(args[1]);
        Path profileFile =
                args.length > 2
                        ? Path.of(args[2])
                        : root.resolve("profiles/free-09-chinese-20221230-nxp-rc45/profile.json");
        var p = Json.object(Json.parse(Files.readString(profileFile)));
        var originals = Json.obj(p, "original");

        voiceWiring(p);
        checkMiswiringRejected(p);
        checkConfigure(p);
        checkResources(p, originals, inputs);
        checkSystemPolicy(p, originals, inputs);
        System.out.println(
                "NXP Voice inputs PASS: 204 actual STT licenses, five configs, six real wakeup/Echo"
                    + " models and32default ASR/Vprint/semantic files; exact"
                    + " partition/vehicle/guest mapping, hashes, extents, single install and system"
                    + " ownership; original local IVI vendor configuration; SystemPolicy OEM APK"
                    + " installed once with the platform signing patch");
    }

    static void checkMiswiringRejected(Map<String, Object> p) {
        var wrong = Json.object(Json.parse(Json.write(p)));
        var wrongPatches = Json.obj(wrong, "patches");
        var wrongSetting = Json.obj(wrongPatches, "Setting");
        var wrongSystemUi = Json.obj(wrongPatches, "SystemUI");
        var settingSteps = Json.list(wrongSetting.get("steps"));
        var systemUiSteps = Json.list(wrongSystemUi.get("steps"));

        settingSteps.remove(NxpVoiceInitializationTest.NAME);
        systemUiSteps.add(NxpVoiceInitializationTest.NAME);
        wrongSetting.put("steps", settingSteps);
        wrongSystemUi.put("steps", systemUiSteps);
        wrongPatches.put("Setting", wrongSetting);
        wrongPatches.put("SystemUI", wrongSystemUi);
        wrong.put("patches", wrongPatches);
        boolean rejected = false;

        try {
            voiceWiring(wrong);
        } catch (AssertionError expected) {
            rejected = true;
        }

        need(
                rejected,
                "Miswired SystemUI constructor recipe must fail the actual profile graph"
                        + " regression");
    }

    static void checkConfigure(Map<String, Object> p) {
        var properties = Json.obj(Json.obj(p, "configure"), "properties");

        need(
                !properties.containsKey("SpeechResc_init_state"),
                "SpeechResc_init_state must be a boot property, not volatile");
        need(
                properties.get("persist.sys.voice.cal_type").equals("ivi")
                        && properties.get("persist.sys.voice.hotword.dashboard").equals("false"),
                "Actual local OEM engine configuration required");
        need(
                Json.obj(Json.obj(p, "configure"), "bootProperties")
                                .get("ro.qg.voice_vendor")
                                .equals("speech_voicecloud")
                        && Json.obj(Json.obj(p, "configure"), "bootProperties")
                                .get("SpeechResc_init_state")
                                .equals("1"),
                "OEM engine vendor changed");
    }

    static void checkResources(Map<String, Object> p, Map<String, Object> originals, Path inputs)
            throws Exception {
        int stt = 0, config = 0, resources = 0, asr = 0;

        for (var e : originals.entrySet()) {
            var spec = Json.object(e.getValue());

            if (!spec.containsKey("acquisition")
                    || !Json.obj(spec, "acquisition").get("image").equals("resource")) {
                continue;
            }

            String acquisition = Json.str(Json.obj(spec, "acquisition"), "path");
            need(
                    acquisition.startsWith("/resc_qgspeech/aispeech/"),
                    "Wrong resource partition path");
            need(
                    Json.str(Json.obj(spec, "source"), "path").equals("/resc" + acquisition),
                    "Vehicle/resource partition mapping differs");
            Path file = inputs.resolve(Json.str(spec, "file")).toRealPath();
            need(
                    Files.size(file) == Json.integer(spec, "bytes")
                            && FilesSupport.hash(file).equals(Json.str(spec, "sha256")),
                    "Actual selected OEM resource bytes differ");
            String target =
                    "/data/data/android/resc_speech/aispeech/"
                            + acquisition.substring("/resc_qgspeech/aispeech/".length());
            checkResourceInstall(p, e.getKey(), target);

            if (acquisition.contains("/aispeech_license/stt/")) {
                stt++;
            } else if (acquisition.endsWith(".json") || acquisition.endsWith("module_cfg.xml")) {
                config++;
            }

            if (e.getKey().startsWith("voice-asr-model-")) {
                asr++;
            }

            resources++;
        }

        need(
                stt == 204 && config == 5 && asr == 32 && resources == 247,
                "Selected original STT/config/wakeup/Echo resource closure incomplete");
        need(
                originals.keySet().stream()
                        .noneMatch(k ->
                                k.contains("voice-stt-license")
                                        && Json.str(
                                                        Json.obj(
                                                                Json.object(originals.get(k)),
                                                                "acquisition"),
                                                        "path")
                                                .contains("/tts/")),
                "Unrequested TTS licenses included");
    }

    static void checkResourceInstall(Map<String, Object> p, String key, String target) {
        int installs = 0;

        for (var item : Json.list(p.get("install"))) {
            var install = Json.object(item);

            if (install.get("artifact").equals("original:" + key)) {
                need(
                        install.get("type").equals("file")
                                && install.get("target").equals(target)
                                && install.get("mode").equals("0644")
                                && Json.integer(install, "uid") == 1000
                                && Json.integer(install, "gid") == 1000,
                        "OEM resource installation path/owner/mode differs");
                installs++;
            }
        }

        need(installs == 1, "Resource must install exactly once");
    }

    static void checkSystemPolicy(Map<String, Object> p, Map<String, Object> originals, Path inputs)
            throws Exception {
        // The OEM SystemPolicy package must be delivered from the pinned system image with the
        // platform signing patch.
        var systemPolicy = Json.object(originals.get("SystemPolicy"));
        Path policyFile = inputs.resolve(Json.str(systemPolicy, "file")).toRealPath();

        need(
                Files.size(policyFile) == Json.integer(systemPolicy, "bytes")
                        && FilesSupport.hash(policyFile).equals(Json.str(systemPolicy, "sha256")),
                "SystemPolicy OEM bytes differ");
        need(
                Json.str(Json.obj(systemPolicy, "acquisition"), "image").equals("system")
                        && Json.str(Json.obj(systemPolicy, "acquisition"), "path")
                                .equals(
                                        "/system/priv-app/SystemPolicy-release/SystemPolicy-release.apk"),
                "SystemPolicy must be the actual acquired privileged APK");
        int policyInstalls = 0;

        for (var item : Json.list(p.get("install"))) {
            var install = Json.object(item);

            if (install.get("artifact").equals("patched:SystemPolicy")) {
                need(
                        install.get("type").equals("apk")
                                && install.get("target")
                                        .equals(
                                                "/system/priv-app/SystemPolicy-release/SystemPolicy-release.apk")
                                && install.get("package").equals("com.qinggan.systempolicy"),
                        "SystemPolicy installation differs from the pinned OEM privileged"
                                + " contract");
                policyInstalls++;
            }
        }

        need(policyInstalls == 1, "SystemPolicy must be installed exactly once");
        need(
                "platform".equals(Json.obj(Json.obj(p, "patches"), "SystemPolicy").get("sign")),
                "SystemPolicy must match the system shared UID signer");
    }
}
