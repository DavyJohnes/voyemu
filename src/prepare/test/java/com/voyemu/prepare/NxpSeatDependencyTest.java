package com.voyemu.prepare;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.reference.StringReference;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipFile;

/** Launcher SeatCard must resolve to the real OEM secondary package, not its primary sibling. */
public final class NxpSeatDependencyTest {
    static final String ID = "free-09-chinese-20221230-nxp-rc45",
            NAME = "VehicleCenterSecond",
            PACKAGE = "com.qinggan.app.vehicle.second";
    static final String PIN = "f38a6ff636c35da81d947e26a42350dd82a10bb1c0b61377cc236b753383c548",
            SEAT = "com.qinggan.app.vehicle.vehicle.fragments.air_and_seat.SeatActivity";

    static void need(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static void recipe(Map<String, Object> profile) {
        var originals = Json.obj(profile, "original");

        need(originals.containsKey(NAME), "Missing OEM secondary VehicleCenter");
        var original = Json.obj(originals, NAME);
        need(
                PIN.equals(original.get("sha256"))
                        && Long.valueOf(31272064)
                                .equals(((Number) original.get("bytes")).longValue()),
                "Secondary original pin differs");
        need(
                "vehiclecentersecond.apk".equals(original.get("file")),
                "Secondary original file differs");
        need(
                "/system/priv-app/VehicleCenter-second-signed/VehicleCenter-second-signed.apk"
                        .equals(Json.obj(original, "acquisition").get("path")),
                "Wrong OEM acquisition path");
        var patch = Json.obj(Json.obj(profile, "patches"), NAME);
        need(
                "original:VehicleCenterSecond".equals(patch.get("input"))
                        && "platform".equals(patch.get("sign")),
                "Secondary patch must use its own OEM APK/platform signer");
        need(
                Json.list(patch.get("uses")).contains("component:qg-runtime")
                        && Json.list(patch.get("steps")).contains("add-qg-runtime.json"),
                "Secondary NVRAM runtime missing");
        int installed = 0;

        for (Object item : Json.list(profile.get("install"))) {
            var install = Json.object(item);

            if (!"patched:VehicleCenterSecond".equals(install.get("artifact"))) {
                continue;
            }

            installed++;
            need(
                    "apk".equals(install.get("type")) && PACKAGE.equals(install.get("package")),
                    "Secondary install package differs");
        }

        need(installed == 1, "Secondary APK must be installed exactly once");
    }

    static int u16(byte[] b, int p) {
        return (b[p] & 255) | ((b[p + 1] & 255) << 8);
    }

    static int u32(byte[] b, int p) {
        return u16(b, p) | (u16(b, p + 2) << 16);
    }

    static boolean manifest(byte[] xml) {
        var strings = new ArrayList<String>();
        boolean correctPackage = false, activity = false;

        need(u16(xml, 0) == 3, "OEM binary manifest required");

        for (int at = 8; at < xml.length; ) {
            int kind = u16(xml, at), size = u32(xml, at + 4);
            need(size >= 8 && size <= xml.length - at, "Manifest extent");

            if (kind == 1) {
                need(u32(xml, at + 16) == 0, "OEM UTF16 pool required");
                int count = u32(xml, at + 8), start = u32(xml, at + 20);

                for (int i = 0; i < count; i++) {
                    int text = at + start + u32(xml, at + 28 + i * 4), length = u16(xml, text);
                    need(length < 0x8000, "Manifest string extent");
                    strings.add(new String(xml, text + 2, length * 2, StandardCharsets.UTF_16LE));
                }
            } else if (kind == 0x102) {
                String tag = strings.get(u32(xml, at + 20));
                int start = at + 16 + u16(xml, at + 24),
                        width = u16(xml, at + 26),
                        count = u16(xml, at + 28);
                var attrs = new HashMap<String, String>();

                for (int i = 0; i < count; i++) {
                    int value = start + i * width,
                            index = u32(xml, value + 16),
                            type = xml[value + 15] & 255;
                    attrs.put(
                            strings.get(u32(xml, value + 4)),
                            type == 3 ? strings.get(index) : Integer.toString(index));
                }

                if (tag.equals("manifest")) {
                    correctPackage = PACKAGE.equals(attrs.get("package"));
                }

                if (tag.equals("activity") && SEAT.equals(attrs.get("name"))) {
                    activity =
                            "-1".equals(attrs.get("exported")) || "1".equals(attrs.get("exported"));
                }
            }

            at += size;
        }

        return correctPackage && activity;
    }

    static byte[] manifest(Path apk) throws Exception {
        try (ZipFile zip = new ZipFile(apk.toFile())) {
            return zip.getInputStream(zip.getEntry("AndroidManifest.xml")).readAllBytes();
        }
    }

    static Map<String, Object> copy(Map<String, Object> value) {
        return Json.object(Json.parse(Json.write(value)));
    }

    static void reject(Map<String, Object> value) {
        try {
            recipe(value);
            throw new AssertionError("Missing secondary dependency accepted");
        } catch (AssertionError expected) {
            need(
                    !expected.getMessage().equals("Missing secondary dependency accepted"),
                    "Negative check failed");
        }
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]);
        var profile =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve("profiles/" + ID + "/profile.json"))));

        recipe(profile);
        Path inputs = root.resolve(".local/profiles/" + ID + "/originals"),
                secondary = inputs.resolve("vehiclecentersecond.apk");
        NativeCore.pinned(secondary, PIN);
        need(
                manifest(manifest(secondary)),
                "Secondary OEM manifest does not expose requested SeatActivity");
        need(
                !manifest(manifest(inputs.resolve("vehiclecenter.apk"))),
                "Primary APK incorrectly accepted as secondary");
        boolean packageReference = false;
        var dex =
                DexFileFactory.loadDexContainer(
                        inputs.resolve("launcher.apk").toFile(), Opcodes.forApi(28));

        for (String entry : dex.getDexEntryNames()) {
            for (var type : dex.getEntry(entry).getDexFile().getClasses()) {
                if (type.getType().equals("Lcom/qinggan/app/vehicle/second/card/SeatCardView;")) {
                    for (var method : type.getMethods()) {
                        if (method.getName().equals("jumpSeat")) {
                            for (var instruction : method.getImplementation().getInstructions()) {
                                if (instruction instanceof ReferenceInstruction ref
                                        && ref.getReference() instanceof StringReference text
                                        && PACKAGE.equals(text.getString())) {
                                    packageReference = true;
                                }
                            }
                        }
                    }
                }
            }
        }

        need(packageReference, "Actual Launcher SeatCard target changed");
        var missing = copy(profile);
        var originals = Json.obj(missing, "original");
        originals.remove(NAME);
        missing.put("original", originals);
        reject(missing);
        var wrong = copy(profile);
        var patches = Json.obj(wrong, "patches");
        var patch = Json.obj(patches, NAME);
        patch.put("input", "original:VehicleCenter");
        patches.put(NAME, patch);
        wrong.put("patches", patches);
        reject(wrong);
        var runtime = copy(profile);
        patches = Json.obj(runtime, "patches");
        patch = Json.obj(patches, NAME);
        patch.put("uses", List.of());
        patches.put(NAME, patch);
        runtime.put("patches", patches);
        reject(runtime);
        var absent = copy(profile);
        var installs = Json.list(absent.get("install"));
        installs.removeIf(
                item -> "patched:VehicleCenterSecond".equals(Json.object(item).get("artifact")));
        absent.put("install", installs);
        reject(absent);
        System.out.println(
                "NXP Seat dependency PASS: actual Launcher target, pinned OEM secondary manifest,"
                        + " wrong primary/missing original/install/runtime negatives");
    }
}
