package com.voyemu.oem;

import java.io.File;
import java.nio.file.Files;
import java.nio.file.Paths;

public final class CameraCapabilityPolicyProbe {
    private static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    public static void main(String[] args) throws Exception {
        checkCapabilityAndQuotaContracts();

        if (args.length != 1) {
            throw new IllegalArgumentException("project-owned test root required");
        }

        File root =
                Files.createTempDirectory(Paths.get(args[0]), "camera-storage-policy-").toFile();
        File first = CameraStoragePolicy.reserve(root, 1, "20261012_123456");
        check(first.getName().equals("IMG_camera_20261012_123456.jpg"));
        Files.write(first.toPath(), new byte[] {1, 2, 3});
        File second = CameraStoragePolicy.reserve(root, 1, "20261012_123456");
        check(!first.equals(second));
        check(Files.readAllBytes(first.toPath()).length == 3);
        check(second.getCanonicalPath().startsWith(root.getCanonicalPath() + File.separator));
        check(
                CameraStoragePolicy.reserve(root, 2, "20261012_123456")
                        .getName()
                        .endsWith(".mp4.tmp"));

        try {
            CameraStoragePolicy.reserve(root, 3, "20261012_123456");
            throw new AssertionError();
        } catch (IllegalArgumentException expected) {
            // The call must reject this input.
        }

        try {
            CameraStoragePolicy.reserve(root, 1, "../20261012_123456");
            throw new AssertionError();
        } catch (IllegalArgumentException expected) {
            // The call must reject this input.
        }

        File unsafe =
                Files.createTempDirectory(Paths.get(args[0]), "camera-storage-outside-").toFile();
        File attacked =
                Files.createTempDirectory(Paths.get(args[0]), "camera-storage-symlink-").toFile();
        Files.createSymbolicLink(new File(attacked, "voyemu-camera").toPath(), unsafe.toPath());

        try {
            CameraStoragePolicy.directory(attacked);
            throw new AssertionError();
        } catch (java.io.IOException expected) {
            // The call must reject this input.
        }

        check(!new File(unsafe, "lvpai").exists());
        check(CameraStoragePolicy.latestCompletedJpeg(root) == null);
        byte[] jpeg = new byte[] {(byte) 255, (byte) 216, 1, (byte) 255, (byte) 217};
        Files.write(first.toPath(), jpeg);
        check(CameraStoragePolicy.latestCompletedJpeg(root).equals(first.getCanonicalFile()));
        File directory = CameraStoragePolicy.directory(root);
        Files.write(new File(directory, "arbitrary.jpg").toPath(), jpeg);
        Files.write(new File(directory, "IMG_camera_20261012_123458.jpg.tmp").toPath(), jpeg);
        Files.write(
                new File(directory, "IMG_camera_20261012_123459.jpg").toPath(),
                new byte[] {(byte) 255, (byte) 216, 1, 2, 3});
        File outside = new File(unsafe, "outside.jpg");
        Files.write(outside.toPath(), jpeg);
        Files.createSymbolicLink(
                new File(directory, "IMG_camera_20261012_123460.jpg").toPath(), outside.toPath());
        check(CameraStoragePolicy.latestCompletedJpeg(root).equals(first.getCanonicalFile()));
        File newer = CameraStoragePolicy.reserve(root, 1, "20261012_123457");
        Files.write(newer.toPath(), jpeg);
        check(newer.setLastModified(first.lastModified() + 1000));
        check(CameraStoragePolicy.latestCompletedJpeg(root).equals(newer.getCanonicalFile()));
        File video = new File(directory, "VID_camera_20261028_120000.mp4");
        Files.write(video.toPath(), new byte[] {1, 2, 3});
        Files.write(
                new File(directory, "VID_camera_20261028_120001.mp4.tmp").toPath(), new byte[] {1});
        File videoLink = new File(directory, "VID_camera_20261028_120002.mp4");
        Files.createSymbolicLink(videoLink.toPath(), outside.toPath());
        File[] album = CameraStoragePolicy.completedMedia(root);
        check(album.length == 3);

        for (File recording : album) {
            check(!recording.equals(videoLink) && !recording.getName().endsWith(".tmp"));
        }

        try {
            CameraStoragePolicy.deleteCompletedMedia(root, outside);
            throw new AssertionError();
        } catch (java.io.IOException expected) {
            check(outside.isFile());
        }

        check(CameraStoragePolicy.deleteCompletedMedia(root, video) && !video.exists());

        for (int n = 0; n < 101; n++) {
            Files.write(new File(directory, "noise-" + n).toPath(), new byte[0]);
        }

        try {
            CameraStoragePolicy.latestCompletedJpeg(root);
            throw new AssertionError();
        } catch (java.io.IOException expected) {
            check(expected.getMessage().equals("camera-gallery-entry-limit"));
        }
    }

    private static void checkCapabilityAndQuotaContracts() {
        check(!CameraCapabilityPolicy.supportsDvr("none", false));
        check(!CameraCapabilityPolicy.supportsDvr("none", true));
        check(!CameraCapabilityPolicy.supportsDvr("android-back-camera", false));
        check(CameraCapabilityPolicy.supportsDvr("android-back-camera", true));

        try {
            CameraCapabilityPolicy.supportsDvr("physical-dvr", true);
            throw new AssertionError();
        } catch (IllegalArgumentException expected) {
            // The call must reject this input.
        }

        check(CameraCapabilityPolicy.dmsCoordinationMarker(0) == 0);
        check(CameraCapabilityPolicy.dmsCoordinationMarker(1) == 1);

        for (int value : new int[] {-1, 2, Integer.MAX_VALUE}) {
            try {
                CameraCapabilityPolicy.dmsCoordinationMarker(value);
                throw new AssertionError();
            } catch (IllegalArgumentException expected) {
                // The call must reject this input.
            }
        }

        check(CameraStoragePolicy.freePercent(0, 100) == 0);
        check(CameraStoragePolicy.freePercent(10, 0) == 0);
        check(CameraStoragePolicy.freePercent(-1, 100) == 0);
        check(CameraStoragePolicy.freePercent(50, 100) == 50);
        check(CameraStoragePolicy.freePercent(150, 100) == 100);
        check(CameraStoragePolicy.freePercent(Long.MAX_VALUE / 2, Long.MAX_VALUE) > 49);
        CameraStoragePolicy.requireGalleryRoot("/gallery/");

        try {
            CameraStoragePolicy.requireGalleryRoot("/gallery/../");
            throw new AssertionError();
        } catch (IllegalArgumentException expected) {
            // The call must reject this input.
        }
    }
}
