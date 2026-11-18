package com.voyemu.oem;

import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.SeekableByteChannel;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;

/**
 * Explicit virtual-camera storage, confined to the Camera application's private files directory.
 */
public final class CameraStoragePolicy {
    private CameraStoragePolicy() {}

    public static float freePercent(long available, long total) {
        if (available <= 0 || total <= 0) {
            return 0.0f;
        }

        return (float) Math.min(100.0d, ((double) available / (double) total) * 100.0d);
    }

    public static void requireGalleryRoot(String originalPath) {
        if (!"/gallery/".equals(originalPath)) {
            throw new IllegalArgumentException("unsupported-camera-storage-path");
        }
    }

    public static File directory(File appFiles) throws IOException {
        if (appFiles == null || !appFiles.isDirectory()) {
            throw new IOException("camera-files-unavailable");
        }

        File base = appFiles.getCanonicalFile();
        File directory = new File(base, "voyemu-camera/lvpai");
        File canonical = directory.getCanonicalFile();

        if (!canonical.toPath().startsWith(base.toPath()) || canonical.equals(base)) {
            throw new IOException("camera-storage-escape");
        }

        if (!directory.isDirectory() && !directory.mkdirs()) {
            throw new IOException("camera-storage-mkdir");
        }

        if (!directory.getCanonicalFile().equals(canonical) || !directory.isDirectory()) {
            throw new IOException("camera-storage-changed");
        }

        return canonical;
    }

    /** Reserve a new own name; never overwrite a previous user capture within the same second. */
    public static File reserve(File appFiles, int type, String timestamp) throws IOException {
        if (type != 1 && type != 2) {
            throw new IllegalArgumentException("invalid-camera-media-type");
        }

        if (timestamp == null || !timestamp.matches("[0-9]{8}_[0-9]{6}")) {
            throw new IllegalArgumentException("invalid-camera-timestamp");
        }

        File directory = directory(appFiles);
        String prefix = type == 1 ? "IMG_camera_" : "VID_camera_";
        String suffix = type == 1 ? ".jpg" : ".mp4.tmp";

        for (int attempt = 0; attempt < 100; attempt++) {
            File output =
                    new File(
                            directory,
                            prefix + timestamp + (attempt == 0 ? "" : "_" + attempt) + suffix);

            if (!output.getCanonicalFile().getParentFile().equals(directory)) {
                throw new IOException("camera-output-escape");
            }

            if (output.createNewFile()) {
                return output;
            }
        }

        throw new IOException("camera-output-name-exhausted");
    }

    /** At most 100 directory entries; never recurse, follow symlinks, or expose a partial JPEG. */
    public static File latestCompletedJpeg(File appFiles) throws IOException {
        File directory = directory(appFiles);
        File latest = null;
        long modified = Long.MIN_VALUE;
        int count = 0;

        try (DirectoryStream<Path> entries = Files.newDirectoryStream(directory.toPath())) {
            for (Path entry : entries) {
                if (++count > 100) {
                    throw new IOException("camera-gallery-entry-limit");
                }

                if (!entry.getFileName()
                                .toString()
                                .matches("IMG_camera_[0-9]{8}_[0-9]{6}(_[1-9][0-9]?)?\\.jpg")
                        || !Files.isRegularFile(entry, LinkOption.NOFOLLOW_LINKS)) {
                    continue;
                }

                File file = entry.toFile().getCanonicalFile();

                if (!file.getParentFile().equals(directory) || !completedJpeg(file)) {
                    continue;
                }

                long time = file.lastModified();

                if (latest == null
                        || time > modified
                        || (time == modified && file.getName().compareTo(latest.getName()) > 0)) {
                    latest = file;
                    modified = time;
                }
            }
        }

        return latest;
    }

    public static File[] completedMedia(File appFiles) throws IOException {
        File directory = directory(appFiles);
        java.util.List<File> media = new java.util.ArrayList<>();
        int count = 0;

        try (DirectoryStream<Path> entries = Files.newDirectoryStream(directory.toPath())) {
            for (Path entry : entries) {
                if (++count > 100) {
                    throw new IOException("camera-gallery-entry-limit");
                }

                if (!entry.getFileName()
                                .toString()
                                .matches(
                                        "(IMG|VID)_camera_[0-9]{8}_[0-9]{6}(_[1-9][0-9]?)?\\.(jpg|mp4)")
                        || !Files.isRegularFile(entry, LinkOption.NOFOLLOW_LINKS)) {
                    continue;
                }

                File file = entry.toFile().getCanonicalFile();

                if (!file.getParentFile().equals(directory) || file.length() == 0) {
                    continue;
                }

                if (file.getName().endsWith(".jpg") && !completedJpeg(file)) {
                    continue;
                }

                media.add(file);
            }
        }

        media.sort((a, b) -> b.getName().compareTo(a.getName()));

        return media.toArray(new File[0]);
    }

    public static boolean deleteCompletedMedia(File appFiles, File candidate) throws IOException {
        for (File file : completedMedia(appFiles)) {
            if (file.equals(candidate) && !Files.isSymbolicLink(candidate.toPath())) {
                return Files.deleteIfExists(file.toPath());
            }
        }

        throw new IOException("camera-recording-outside-album");
    }

    private static boolean completedJpeg(File file) throws IOException {
        long length = file.length(), modified = file.lastModified();

        if (length < 4 || length > 64L * 1024 * 1024) {
            return false;
        }

        try (SeekableByteChannel bytes =
                Files.newByteChannel(
                        file.toPath(), StandardOpenOption.READ, LinkOption.NOFOLLOW_LINKS)) {
            ByteBuffer marker = ByteBuffer.allocate(2);

            if (bytes.size() != length
                    || bytes.read(marker) != 2
                    || (marker.get(0) & 255) != 255
                    || (marker.get(1) & 255) != 216) {
                return false;
            }

            marker.clear();
            bytes.position(length - 2);

            return bytes.read(marker) == 2
                    && (marker.get(0) & 255) == 255
                    && (marker.get(1) & 255) == 217
                    && bytes.size() == length
                    && file.lastModified() == modified;
        }
    }
}
