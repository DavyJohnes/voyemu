package com.voyemu.prepare;

import org.jf.dexlib2.AccessFlags;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Real generated DEX, signing, metadata and failed producer contracts. */
public final class PassionGeneratedHelperContractTest {
    interface Run {
        void run() throws Exception;
    }

    static void check(boolean ok) {
        if (!ok) {
            throw new AssertionError("helper contract");
        }
    }

    static void reject(Run run) throws Exception {
        try {
            run.run();
            throw new AssertionError("invalid producer accepted");
        } catch (Failure expected) {
            check(expected.code.equals("PASSION_GENERATED_HELPER"));
        }
    }

    static byte[] missingHooks(byte[] dex) throws Exception {
        var types = new ArrayList<ClassDef>();

        for (var type : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            types.add(
                    new ImmutableClassDef(
                            type.getType(),
                            type.getAccessFlags(),
                            type.getSuperclass(),
                            type.getInterfaces(),
                            type.getSourceFile(),
                            type.getAnnotations(),
                            type.getStaticFields(),
                            type.getInstanceFields(),
                            List.of(),
                            type.getVirtualMethods()));
        }

        return FrameworkDexMaterializer.serialize(new ImmutableDexFile(Opcodes.forApi(30), types));
    }

    static byte[] inaccessibleOwner(byte[] dex, boolean sound) throws Exception {
        var types = new ArrayList<ClassDef>();
        String owner =
                sound
                        ? "Lcom/voyemu/settings/PassionSoundStreams;"
                        : "Lcom/voyemu/media/PassionUsbMediaSession;";

        for (var type : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            types.add(
                    new ImmutableClassDef(
                            type.getType(),
                            type.getType().equals(owner)
                                    ? type.getAccessFlags() & ~AccessFlags.PUBLIC.getValue()
                                    : type.getAccessFlags(),
                            type.getSuperclass(),
                            type.getInterfaces(),
                            type.getSourceFile(),
                            type.getAnnotations(),
                            type.getStaticFields(),
                            type.getInstanceFields(),
                            type.getDirectMethods(),
                            type.getVirtualMethods()));
        }

        return FrameworkDexMaterializer.serialize(new ImmutableDexFile(Opcodes.forApi(30), types));
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), platform = Path.of(args[1]), work = Path.of(args[2]);

        Files.createDirectory(work);
        var declared =
                Map.<String, Object>of(
                        "CarSettings",
                        Map.of("steps", List.of("passion-api30-settings-sound.json")),
                        "LY_Multimedia",
                        Map.of("steps", List.of("passion-api30-usb-media-session.json")));
        check(
                PassionGeneratedHelperContract.requested(
                                PassionSettingsSoundGuard.PROFILE, declared)
                        .equals(
                                Set.of(
                                        PassionGeneratedHelperContract.SOUND,
                                        PassionGeneratedHelperContract.USB)));
        check(PassionGeneratedHelperContract.requested("foreign", declared).isEmpty());
        check(
                PassionGeneratedHelperContract.requested(
                                PassionSettingsSoundGuard.PROFILE, Map.of())
                        .isEmpty());
        check(
                PassionGeneratedHelperContract.requested(
                                PassionSettingsSoundGuard.PROFILE,
                                Map.of(
                                        "CarSettings",
                                        Map.of("steps", List.of()),
                                        "LY_Multimedia",
                                        Map.of("steps", List.of())))
                        .isEmpty());
        var signing =
                Json.obj(
                        Json.obj(
                                Json.object(
                                        Json.parse(
                                                Files.readString(
                                                        root.resolve("src/app/toolchain.json")))),
                                "signing"),
                        "platform");
        byte[] key = Files.readAllBytes(SigningIntermediateTest.signingInput(root, signing, "key")),
                cert =
                        Files.readAllBytes(
                                SigningIntermediateTest.signingInput(root, signing, "cert"));
        Path ownApk = SigningIntermediateTest.ownApk(root, work);

        for (String artifact :
                Set.of(PassionGeneratedHelperContract.SOUND, PassionGeneratedHelperContract.USB)) {
            boolean sound = artifact.equals(PassionGeneratedHelperContract.SOUND);
            String name =
                    sound ? "passion-api30-settings-sound" : "passion-api30-usb-media-session";
            Path source =
                    root.resolve(
                            "profiles/"
                                    + PassionSettingsSoundGuard.PROFILE
                                    + "/patches/"
                                    + name
                                    + "/src");
            Path original =
                    root.resolve(
                            ".local/profiles/"
                                    + PassionSettingsSoundGuard.PROFILE
                                    + "/originals/"
                                    + (sound ? "CarSettings.apk" : "LY_Multimedia.apk"));
            Path helper =
                    sound
                            ? PassionSettingsSoundGuard.compileHelper(
                                    PassionSettingsSoundGuard.PROFILE,
                                    30,
                                    original,
                                    source,
                                    platform,
                                    work.resolve(name))
                            : PassionUsbMediaSessionGuard.compileHelper(
                                    PassionSettingsSoundGuard.PROFILE,
                                    30,
                                    original,
                                    source,
                                    platform,
                                    work.resolve(name));
            byte[] dex = Files.readAllBytes(helper);
            reject(() -> PassionGeneratedHelperContract.verify(artifact, missingHooks(dex)));
            reject(() ->
                    PassionGeneratedHelperContract.verify(artifact, inaccessibleOwner(dex, sound)));
            reject(() -> PassionGeneratedHelperContract.verify(artifact, null));
            reject(() -> PassionGeneratedHelperContract.verify("unknown", dex));
            var entries = NativeCore.entries(ownApk);
            entries.keySet().removeIf(DexOperations::signatureEntry);
            entries.put(PassionGeneratedHelperContract.slot(artifact), dex);
            Path unsigned = work.resolve(name + "-unsigned.apk"),
                    signed = work.resolve(name + "-signed.apk");
            NativeCore.zip(unsigned, entries, true);
            DexOperations.sign(unsigned, signed, key, cert, 30);
            var outputs =
                    PreparationMain.artifactOutputs(
                            Map.of(artifact, signed), false, Set.of(artifact));
            var receipt = Json.obj(Json.obj(outputs, artifact), "generated_helper");
            check(
                    receipt.size() == 6
                            && receipt.get("archive_sha256").equals(FilesSupport.hash(signed))
                            && receipt.get("sha256").equals(NativeCore.hash(dex))
                            && receipt.get("bytes").equals((long) dex.length));
            check(
                    Json.obj(
                                            PreparationMain.artifactOutputs(
                                                    Map.of(artifact, signed), false),
                                            artifact)
                                    .size()
                            == 3);
            reject(() -> PreparationMain.artifactOutputs(Map.of(), false, Set.of(artifact)));
            entries.put(
                    PassionGeneratedHelperContract.slot(artifact), inaccessibleOwner(dex, sound));
            Path inaccessibleUnsigned = work.resolve(name + "-inaccessible-unsigned.apk"),
                    inaccessibleSigned = work.resolve(name + "-inaccessible-signed.apk");
            NativeCore.zip(inaccessibleUnsigned, entries, true);
            DexOperations.sign(inaccessibleUnsigned, inaccessibleSigned, key, cert, 30);
            reject(() ->
                    PreparationMain.artifactOutputs(
                            Map.of(artifact, inaccessibleSigned), false, Set.of(artifact)));
            Path wrong = work.resolve(name + "-wrong.apk");
            NativeCore.zip(wrong, Map.of("classes.dex", dex), false);
            reject(() ->
                    PreparationMain.artifactOutputs(
                            Map.of(artifact, wrong), false, Set.of(artifact)));
        }

        reject(() -> PreparationMain.artifactOutputs(Map.of(), false, Set.of("unknown")));
        System.out.println(
                "PASS real Sound/USB DEX + signed artifact receipts; missing ABI/entry/artifact,"
                        + " unknown and absent graph");
    }
}
