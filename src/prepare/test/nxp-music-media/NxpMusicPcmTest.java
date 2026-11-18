import android.os.Environment;

import com.voyemu.output.NxpMusicPcm;

import java.nio.file.Files;
import java.nio.file.Path;

public final class NxpMusicPcmTest {
    private static void need(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static void put(byte[] bytes, int offset, long value, int count) {
        for (int i = 0; i < count; i++) {
            bytes[offset + i] = (byte) (value >> (8 * i));
        }
    }

    private static byte[] wave(int channels, int rate) {
        byte[] bytes = new byte[52];

        put(bytes, 0, 0x46464952L, 4);
        put(bytes, 4, 44, 4);
        put(bytes, 8, 0x45564157L, 4);
        put(bytes, 12, 0x20746d66L, 4);
        put(bytes, 16, 16, 4);
        put(bytes, 20, 1, 2);
        put(bytes, 22, channels, 2);
        put(bytes, 24, rate, 4);
        put(bytes, 28, rate * channels * 2, 4);
        put(bytes, 32, channels * 2, 2);
        put(bytes, 34, 16, 2);
        put(bytes, 36, 0x61746164L, 4);
        put(bytes, 40, 8, 4);

        return bytes;
    }

    private static void expect(Path path, byte[] bytes, boolean accepted, String message)
            throws Exception {
        Files.write(path, bytes);
        need(NxpMusicPcm.isSupportedWave(path.toString()) == accepted, message);
    }

    public static void main(String[] args) throws Exception {
        Path directory = Files.createTempDirectory("nxp-pcm-");
        Path path = directory.resolve("sample.wav");

        try {
            need(!NxpMusicPcm.isSupportedWave(null), "null accepted");
            need(!NxpMusicPcm.isSupportedWave("relative.wav"), "relative path accepted");
            need(!NxpMusicPcm.isSupportedWave("https://example/test.wav"), "URL accepted");
            need(!NxpMusicPcm.isSupportedWave(path.toString()), "missing file accepted");
            need(
                    !NxpMusicPcm.isSupportedWave(directory.toString() + ".wav"),
                    "missing directory accepted");
            expect(path, wave(1, 8000), true, "mono PCM rejected");
            expect(path, wave(2, 44100), true, "stereo PCM rejected");
            expect(path, wave(2, 48000), true, "48k PCM rejected");
            expect(path, new byte[52], false, "renamed fake accepted");
            expect(path, new byte[43], false, "short header accepted");
            int[] offsets = {0, 4, 8, 12, 16, 20, 22, 24, 28, 32, 34, 36, 40};

            for (int offset : offsets) {
                byte[] bytes = wave(2, 44100);
                bytes[offset] ^= 0x40;
                expect(path, bytes, false, "invalid field accepted at " + offset);
            }

            expect(path, wave(3, 44100), false, "surround accepted");
            expect(path, wave(2, 7999), false, "low rate accepted");
            expect(path, wave(2, 48001), false, "high rate accepted");
            byte[] truncated = java.util.Arrays.copyOf(wave(2, 44100), 48);
            expect(path, truncated, false, "truncated data accepted");
            byte[] trailing = java.util.Arrays.copyOf(wave(2, 44100), 56);
            expect(path, trailing, false, "trailing bytes accepted");
            byte[] unaligned = java.util.Arrays.copyOf(wave(2, 44100), 51);
            put(unaligned, 4, 43, 4);
            put(unaligned, 40, 7, 4);
            expect(path, unaligned, false, "unaligned data accepted");
            byte[] empty = java.util.Arrays.copyOf(wave(2, 44100), 44);
            put(empty, 4, 36, 4);
            put(empty, 40, 0, 4);
            expect(path, empty, false, "empty data accepted");

            for (String suffix : new String[] {".WAV", ".mp3", ".kwm"}) {
                Path other = directory.resolve("other" + suffix);

                try {
                    expect(other, wave(2, 44100), false, "unsupported suffix accepted");
                } finally {
                    Files.deleteIfExists(other);
                }
            }

            Path shared = Files.createDirectory(directory.resolve("shared"));
            Path sibling = Files.createDirectory(directory.resolve("shared-other"));
            Path inside = shared.resolve("inside.wav");
            Path outside = sibling.resolve("outside.wav");
            java.io.File oldDirectory = Environment.directory;
            String oldState = Environment.state;

            try {
                Files.write(inside, wave(2, 44100));
                Files.write(outside, wave(2, 44100));
                Environment.directory = shared.toFile();
                Environment.state = Environment.MEDIA_MOUNTED;
                need(
                        NxpMusicPcm.isMountedSharedWave(inside.toString()),
                        "mounted shared PCM rejected");
                need(
                        !NxpMusicPcm.isMountedSharedWave("relative.wav"),
                        "shared relative path accepted");
                need(!NxpMusicPcm.isMountedSharedWave(null), "shared null accepted");
                need(!NxpMusicPcm.isMountedSharedWave(shared.toString()), "root itself accepted");
                need(
                        !NxpMusicPcm.isMountedSharedWave(outside.toString()),
                        "prefix sibling accepted");
                need(
                        !NxpMusicPcm.isMountedSharedWave(path.toString()),
                        "outside shared root accepted");
                need(
                        !NxpMusicPcm.isMountedSharedWave(
                                shared.resolve("..")
                                        .resolve("shared-other")
                                        .resolve("outside.wav")
                                        .toString()),
                        "canonical traversal escape accepted");
                Path link = null;

                try {
                    link = Files.createSymbolicLink(shared.resolve("escape.wav"), outside);
                } catch (java.nio.file.FileSystemException
                        | UnsupportedOperationException exception) {
                    System.out.println("SKIP symlink escape: host does not permit symbolic links");
                }

                if (link != null) {
                    try {
                        need(
                                !NxpMusicPcm.isMountedSharedWave(link.toString()),
                                "canonical symlink escape accepted");
                        System.out.println("PASS canonical symlink escape exercised");
                    } finally {
                        Files.delete(link);
                    }
                }

                Environment.state = "unmounted";
                need(
                        !NxpMusicPcm.isMountedSharedWave(inside.toString()),
                        "unmounted shared PCM accepted");
                Environment.state = Environment.MEDIA_MOUNTED;
                Files.write(inside, new byte[52]);
                need(
                        !NxpMusicPcm.isMountedSharedWave(inside.toString()),
                        "malformed shared WAV accepted");
            } finally {
                Environment.directory = oldDirectory;
                Environment.state = oldState;
                Files.deleteIfExists(inside);
                Files.deleteIfExists(outside);
                Files.delete(shared);
                Files.delete(sibling);
            }

            for (String fixture : args) {
                need(NxpMusicPcm.isSupportedWave(fixture), "real fixture rejected: " + fixture);
            }

            System.out.println(
                    "PASS canonical local PCM WAV selector: real fixtures, PCM16 mono/stereo,"
                            + " malformed/unsupported fallthrough");
        } finally {
            Files.deleteIfExists(path);
            Files.delete(directory);
        }
    }
}
