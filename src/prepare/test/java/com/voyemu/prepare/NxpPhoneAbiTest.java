package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * Compiles actual NXP adapter against declarations derived from the pinned OEM DEX, never
 * hand-written owner stubs.
 */
public final class NxpPhoneAbiTest {
    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), work = Path.of(args[1]);

        Files.createDirectories(work);
        Path phone =
                root.resolve(
                        ".local/profiles/free-09-chinese-20221230-nxp-rc45/originals/bluetoothphone.apk");
        NativeCore.pinned(
                phone, "55e7602d15f5d677a5ddeaaa44c0c04c355235c5322b51edefa3a6ef29636451");
        var owner = BluetoothPeerComponent.read(phone);
        String[] names = {
            "CALL_STATE_ACTIVE",
            "CALL_STATE_DIALING",
            "CALL_STATE_INCOMING",
            "CALL_STATE_TERMINATED",
            "CONNECTION_STATE_CONNECTED",
            "CONNECTION_STATE_DISCONNECTED",
            "PBAP_RESPONSE_TYPE_CALLBACK",
            "PBAP_SYNC_STATE_STARTED",
            "PBAP_SYNC_STATE_FINISHED",
            "PBAP_SYNC_STATE_ABORTED",
            "PHONE_NUMBER_TYPE_CELL"
        };
        int[] values = {128, 129, 130, 131, 16, 19, 176, 96, 97, 98, 160};

        for (int i = 0; i < names.length; i++) {
            BluetoothPeerComponent.constant(owner, names[i], values[i]);
        }

        BluetoothPeerComponent.method(owner, "bean/BTContactItem", "<init>()V");
        BluetoothPeerComponent.method(owner, "bean/BTPhoneNumber", "<init>()V");
        BluetoothPeerComponent.field(owner, "bean/BTContactItem", "name", "Ljava/lang/String;");
        BluetoothPeerComponent.field(owner, "bean/BTPhoneNumber", "number", "Ljava/lang/String;");
        BluetoothPeerComponent.field(owner, "bean/BTPhoneNumber", "type", "I");

        Path signatures =
                Signatures.jar(
                        new ArrayList<>(owner.values()),
                        work.resolve("oem-owner-compile-only.jar"));
        Path source =
                root.resolve(
                        "profiles/free-09-chinese-20221230-nxp-rc45/components/phone-bluetooth/src");
        List<Path> own;

        try (var files = Files.list(source)) {
            own = files.filter(p -> p.toString().endsWith(".java")).sorted().toList();
        }

        JavaSources.compile(
                own,
                work.resolve("own-classes"),
                root.resolve(".local/toolchains/java-sdk/platforms/android-34/android.jar"),
                List.of(signatures));
        System.out.println(
                "NXP Phone ABI PASS actual adapter compiled against "
                        + owner.size()
                        + " original OEM owner declarations");
    }
}
