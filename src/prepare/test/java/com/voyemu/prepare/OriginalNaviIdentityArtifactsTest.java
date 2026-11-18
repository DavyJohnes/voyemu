package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public final class OriginalNaviIdentityArtifactsTest {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1";

    static void check(boolean x, String s) {
        if (!x) {
            throw new AssertionError(s);
        }
    }

    static boolean contains(Map<String, byte[]> entries, String descriptor) {
        for (var e : entries.entrySet()) {
            if (e.getKey().matches("classes[0-9]*\\.dex")) {
                for (var c : new DexBackedDexFile(Opcodes.forApi(30), e.getValue()).getClasses()) {
                    if (c.getType().equals(descriptor)) {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    public static void main(String[] a) throws Exception {
        Path root = Path.of(a[0]).toRealPath(),
                request = Path.of(a[1]).toRealPath(),
                work = Path.of(a[2]).toAbsolutePath();
        var selected = Json.obj(Json.object(Json.parse(Files.readString(request))), "tools");
        var sign = Json.obj(selected, "signing");
        var platform = Json.obj(selected, "platform");
        var base = new ProfileRegistry(root, PROFILE).get(PROFILE);
        var data = new HashMap<>(base.data());
        var patches = new HashMap<>(Json.obj(data, "patches"));
        var qgapi = new HashMap<>(Json.obj(patches, "QGAPI"));

        qgapi.put("steps", List.of("api30-audio-consumer.json", "original-navi-vin.json"));
        patches.put("QGAPI", qgapi);
        patches.put(
                "OriginalNaviVehicleTest",
                Map.of(
                        "input",
                        "original:QGVehicle",
                        "type",
                        "jar",
                        "steps",
                        List.of("original-navi-vehicle.json")));
        data.put("patches", patches);
        var profile =
                new ProfileRegistry.Profile(
                        base.id(), base.revision(), data, base.resourcePrefix(), base.sourceRoot());
        var storage =
                new Storage(
                        root.resolve(".local"),
                        work.resolve("config"),
                        work.resolve("cache"),
                        work.resolve("state"),
                        work.resolve("run"),
                        work.resolve("logs"));
        var tools =
                new Toolchains(
                        storage,
                        Json.object(
                                Json.parse(
                                        Files.readString(root.resolve("src/app/toolchain.json")))),
                        Path.of(Json.str(selected, "sdk")),
                        Path.of(Json.str(sign, "key")),
                        Path.of(Json.str(sign, "certificate")),
                        Path.of(Json.str(platform, "file")));
        var art =
                new Artifacts(
                        new Artifacts.PatchedProfile(
                                profile, ProfileRegistry.patchMetadata(profile)),
                        storage,
                        tools,
                        root.resolve("profiles/" + profile.id()),
                        work,
                        p -> {
                            throw new AssertionError("unexpected Android read");
                        },
                        s -> {});
        var vin = NativeCore.entries(art.resolve("patched:QGAPI"));
        var vehicle = NativeCore.entries(art.resolve("patched:OriginalNaviVehicleTest"));
        check(
                contains(vin, "Lcom/voyemu/qgapi/BrokerVin;")
                        && !contains(vin, "Lcom/voyemu/qgapi/VehicleBroker;"),
                "VIN helper isolation");
        check(
                contains(vehicle, "Lcom/voyemu/qgapi/VehicleBroker;")
                        && !contains(vehicle, "Lcom/voyemu/qgapi/BrokerVin;"),
                "vehicle helper isolation");
        check(
                OriginalNaviVinGuard.classes(vin.get("classes.dex"))
                        .containsKey(OriginalNaviVinGuard.OWNER),
                "VIN DEX");
        check(
                OriginalNaviVehicleGuard.classes(vehicle.get("classes.dex"))
                        .containsKey(OriginalNaviVehicleGuard.OWNER),
                "vehicle DEX");
        System.out.println(
                "PASS actual inactive two-pass QGAPI audio→VIN and QGVehicle recipe adapters with"
                        + " isolated helpers");
    }
}
