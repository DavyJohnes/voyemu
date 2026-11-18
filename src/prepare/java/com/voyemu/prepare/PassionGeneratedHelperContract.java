package com.voyemu.prepare;

import org.jf.dexlib2.AccessFlags;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;

import java.nio.file.Path;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

/** Generated helper ABI admission and binding to the actual signed artifact. */
final class PassionGeneratedHelperContract {
    static final String SOUND = "patched:CarSettings";
    static final String USB = "patched:LY_Multimedia";

    private PassionGeneratedHelperContract() {}

    static void need(boolean ok) {
        Failure.require(
                ok, "PASSION_GENERATED_HELPER", "Declared generated helper ABI required", 5);
    }

    static Set<String> requested(String profile, Map<String, Object> patches) {
        Set<String> result = new TreeSet<>();

        if (!PassionSettingsSoundGuard.PROFILE.equals(profile)) {
            return result;
        }

        for (String artifact : Set.of(SOUND, USB)) {
            String name = artifact.substring("patched:".length());
            String step =
                    artifact.equals(SOUND)
                            ? "passion-api30-settings-sound.json"
                            : "passion-api30-usb-media-session.json";

            if (patches.containsKey(name)
                    && Json.list(Json.obj(patches, name).get("steps")).contains(step)) {
                result.add(artifact);
            }
        }

        return result;
    }

    static String slot(String artifact) {
        need(artifact.equals(SOUND) || artifact.equals(USB));

        return artifact.equals(SOUND) ? "classes7.dex" : "classes4.dex";
    }

    static void verify(String artifact, byte[] dex) {
        slot(artifact);
        need(dex != null);
        boolean sound = artifact.equals(SOUND);
        String owner =
                sound
                        ? "Lcom/voyemu/settings/PassionSoundStreams;"
                        : "Lcom/voyemu/media/PassionUsbMediaSession;";
        Set<String> required =
                sound
                        ? Set.of(
                                "update(Landroidx/preference/PreferenceGroup;)V",
                                "dispose(Landroidx/preference/PreferenceGroup;)V")
                        : Set.of(
                                "install(Ljava/lang/Object;Landroid/support/v4/media/session/MediaSessionCompat;)V",
                                "dispose(Ljava/lang/Object;)V");
        Set<String> names = new HashSet<>(), found = new HashSet<>();

        for (var type : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            need(names.add(type.getType()));

            if (!type.getType().equals(owner)) {
                continue;
            }

            need(AccessFlags.PUBLIC.isSet(type.getAccessFlags()));

            for (var method : type.getMethods()) {
                String signature =
                        method.getName()
                                + "("
                                + String.join("", method.getParameterTypes())
                                + ")"
                                + method.getReturnType();

                if (required.contains(signature)) {
                    need(
                            method.getImplementation() != null
                                    && AccessFlags.PUBLIC.isSet(method.getAccessFlags())
                                    && AccessFlags.STATIC.isSet(method.getAccessFlags())
                                    && found.add(signature));
                }
            }
        }

        need(found.equals(required));
        need(
                names.equals(
                        sound
                                ? Set.of(
                                        owner,
                                        "Lcom/voyemu/settings/PassionSoundStreams$1;",
                                        "Lcom/voyemu/settings/PassionSoundStreams$$ExternalSyntheticLambda0;")
                                : Set.of(
                                        owner,
                                        "Lcom/voyemu/media/PassionUsbMediaSession$Bridge;")));
    }

    static Map<String, Object> receipt(String artifact, Path archive) throws Exception {
        byte[] dex = NativeCore.entries(archive).get(slot(artifact));

        verify(artifact, dex);

        return Map.of(
                "contract",
                artifact.equals(SOUND)
                        ? "passion-settings-sound-helper-v1"
                        : "passion-usb-media-session-helper-v1",
                "artifact",
                artifact,
                "archive_sha256",
                FilesSupport.hash(archive),
                "entry",
                slot(artifact),
                "sha256",
                NativeCore.hash(dex),
                "bytes",
                (long) dex.length);
    }
}
