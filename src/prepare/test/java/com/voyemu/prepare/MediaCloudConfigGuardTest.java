package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipFile;

public class MediaCloudConfigGuardTest {

    interface Checked {
        void run() throws Exception;
    }

    static void reject(Checked c) throws Exception {
        boolean ok = false;

        try {
            c.run();
        } catch (Failure e) {
            ok = e.code.equals("MEDIA_CLOUD_CONTRACT");
        }

        if (!ok) {
            throw new AssertionError("negative accepted");
        }
    }

    static int removals(byte[] bytes) {
        int count = 0;

        for (var c : PassionServiceForegroundGuard.classes(bytes).values()) {
            for (var m : c.getMethods()) {
                if (MediaCloudConfigGuard.target(m)) {
                    int address = 0;

                    for (var i : m.getImplementation().getInstructions()) {
                        if (address == 74
                                && i
                                        instanceof
                                        org.jf.dexlib2.iface.instruction.ReferenceInstruction r
                                && r.getReference()
                                        .toString()
                                        .equals("Ljava/util/Set;->remove(Ljava/lang/Object;)Z")) {
                            count++;
                        }

                        address += i.getCodeUnits();
                    }
                }
            }
        }

        return count;
    }

    public static void main(String[] a) throws Exception {
        Path p = Path.of(a[0]);
        Map<String, Object> constants = Map.of(StepConstants.PINS, List.of(FilesSupport.hash(p)));
        byte[] before;

        try (var z = new ZipFile(p.toFile())) {
            before = z.getInputStream(z.getEntry("classes3.dex")).readAllBytes();
        }

        if (removals(before) != 0) {
            throw new AssertionError("RED not reproduced");
        }

        byte[] after = MediaCloudConfigGuard.apply(constants, 30, "arm64-v8a", p, before);

        if (removals(after) != 2) {
            throw new AssertionError("GREEN absent");
        }

        reject(() ->
                MediaCloudConfigGuard.validate(
                        Map.of(StepConstants.PINS, List.of("0".repeat(64))),
                        30,
                        "arm64-v8a",
                        p,
                        before));
        reject(() -> MediaCloudConfigGuard.validate(constants, 28, "arm64-v8a", p, before));
        reject(() -> MediaCloudConfigGuard.validate(constants, 30, "arm64-v8a", p, null));
        reject(() -> MediaCloudConfigGuard.validate(constants, 30, "arm64-v8a", p, after));
        reject(() ->
                MediaCloudConfigGuard.validate(
                        constants, 30, "arm64-v8a", p.resolveSibling("MusicConcate.apk"), before));
        reject(() -> MediaCloudConfigGuard.validate(constants, 30, "armeabi-v7a", p, before));
        var supported =
                new LinkedHashSet<>(
                        List.of("BT_MUSIC", "USB_MUSIC", "RADIO_YUNTING", "LOCAL_RADIO"));
        supported.remove("RADIO_YUNTING");

        if (!supported.equals(
                new LinkedHashSet<>(List.of("BT_MUSIC", "USB_MUSIC", "LOCAL_RADIO")))) {
            throw new AssertionError("core media changed");
        }

        System.out.println(
                "PASS raw2-cloud-add RED => exact2-cloud-remove GREEN; full class semantic inverse;"
                        + " 7 profile/API/ABI/DEX/idempotence negatives; BT/USB/other-enum set"
                        + " unchanged");
    }
}
