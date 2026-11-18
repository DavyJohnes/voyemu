package com.voyemu.output;

import android.os.Environment;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;

/** Selects the OEM Android engine only for canonical local PCM16 WAV files. */
public final class NxpMusicPcm {
    private NxpMusicPcm() {}

    public static boolean isSupportedWave(String path) {
        if (path == null || !path.endsWith(".wav")) {
            return false;
        }

        File file = new File(path);

        if (!file.isAbsolute() || !file.isFile()) {
            return false;
        }

        try (RandomAccessFile input = new RandomAccessFile(file, "r")) {
            long length = input.length();

            if (length < 46) {
                return false;
            }

            byte[] header = new byte[44];
            input.readFully(header);

            if (!isCanonicalPcmHeader(header)) {
                return false;
            }

            return hasValidPcmLayout(header, length);
        } catch (IOException exception) {
            return false;
        }
    }

    private static boolean isCanonicalPcmHeader(byte[] header) {
        return u32(header, 0) == 0x46464952L
                && u32(header, 8) == 0x45564157L
                && u32(header, 12) == 0x20746d66L
                && u32(header, 16) == 16
                && u16(header, 20) == 1
                && u16(header, 34) == 16
                && u32(header, 36) == 0x61746164L;
    }

    private static boolean hasValidPcmLayout(byte[] header, long length) {
        int channels = u16(header, 22);
        long rate = u32(header, 24);
        int alignment = channels * 2;
        long data = u32(header, 40);

        return (channels == 1 || channels == 2)
                && rate >= 8000
                && rate <= 48000
                && u16(header, 32) == alignment
                && u32(header, 28) == rate * alignment
                && data > 0
                && data % alignment == 0
                && data + 44 == length
                && u32(header, 4) + 8 == length;
    }

    public static boolean isMountedSharedWave(String path) {
        if (path == null
                || !Environment.MEDIA_MOUNTED.equals(Environment.getExternalStorageState())) {
            return false;
        }

        try {
            File root = Environment.getExternalStorageDirectory().getCanonicalFile();
            File file = new File(path);

            if (!file.isAbsolute()) {
                return false;
            }

            file = file.getCanonicalFile();

            return file.getPath().startsWith(root.getPath() + File.separator)
                    && isSupportedWave(file.getPath());
        } catch (IOException exception) {
            return false;
        }
    }

    private static int u16(byte[] bytes, int offset) {
        return (bytes[offset] & 255) | ((bytes[offset + 1] & 255) << 8);
    }

    private static long u32(byte[] bytes, int offset) {
        return (long) u16(bytes, offset) | ((long) u16(bytes, offset + 2) << 16);
    }
}
