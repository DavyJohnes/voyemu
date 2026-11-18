package com.voyemu.prepare;

import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.zip.CRC32;

public final class A9OfflineLabelsFixture {
    private static final int SECTOR_SIZE = 512;
    private static final int ENTRY_COUNT = 128;
    private static final int ENTRY_SIZE = 128;
    private static final int HEADER_SIZE = 92;
    private static final int PARTITION_START = 2048;
    private static final int EXT4_FEATURE_INCOMPAT = 1024 + 96;

    private static ByteBuffer littleEndian(byte[] bytes) {
        return ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
    }

    private static int crc32(byte[] bytes, int length) {
        CRC32 crc = new CRC32();

        crc.update(bytes, 0, length);

        return (int) crc.getValue();
    }

    private static byte[] header(
            long current, long backup, long table, long sectors, byte[] entries) {
        byte[] bytes = new byte[SECTOR_SIZE];
        ByteBuffer fields = littleEndian(bytes);

        fields.put("EFI PART".getBytes(StandardCharsets.US_ASCII));
        fields.putInt(8, 0x10000);
        fields.putInt(12, HEADER_SIZE);
        fields.putLong(24, current);
        fields.putLong(32, backup);
        fields.putLong(40, 34);
        fields.putLong(48, sectors - 34);
        Arrays.fill(bytes, 56, 72, (byte) 3);
        fields.putLong(72, table);
        fields.putInt(80, ENTRY_COUNT);
        fields.putInt(84, ENTRY_SIZE);
        fields.putInt(88, crc32(entries, entries.length));
        fields.putInt(16, crc32(bytes, HEADER_SIZE));

        return bytes;
    }

    private static void wrap(Path partition, String image) throws IOException {
        byte[] data = Files.readAllBytes(partition);
        long partitionSectors = data.length / SECTOR_SIZE;
        long sectors = PARTITION_START + partitionSectors + 34;
        byte[] entries = new byte[ENTRY_COUNT * ENTRY_SIZE];

        Arrays.fill(entries, 0, 16, (byte) 1);
        Arrays.fill(entries, 16, 32, (byte) 2);
        ByteBuffer fields = littleEndian(entries);
        fields.putLong(32, PARTITION_START);
        fields.putLong(40, PARTITION_START + partitionSectors - 1);

        try (RandomAccessFile disk = new RandomAccessFile(image, "rw")) {
            disk.setLength(0);
            disk.setLength(sectors * SECTOR_SIZE);
            disk.seek(SECTOR_SIZE - 2);
            disk.writeShort(0x55aa);
            disk.write(header(1, sectors - 1, 2, sectors, entries));
            disk.write(entries);
            disk.seek((long) PARTITION_START * SECTOR_SIZE);
            disk.write(data);
            disk.seek((sectors - 33) * SECTOR_SIZE);
            disk.write(entries);
            disk.write(header(sectors - 1, 1, sectors - 33, sectors, entries));
        }
    }

    public static void main(String[] args) throws IOException {
        switch (args[0]) {
            case "wrap" -> wrap(Path.of(args[1]), args[2]);
            case "corrupt-backup" -> {
                try (RandomAccessFile disk = new RandomAccessFile(args[1], "rw")) {
                    disk.seek(disk.length() - SECTOR_SIZE);
                    disk.write('X');
                }
            }
            case "ext4-incompat" -> {
                try (RandomAccessFile disk = new RandomAccessFile(args[1], "r")) {
                    disk.seek(EXT4_FEATURE_INCOMPAT);
                    System.out.println(
                            Integer.toUnsignedLong(Integer.reverseBytes(disk.readInt())));
                }
            }
            default -> throw new IllegalArgumentException("Unknown fixture command " + args[0]);
        }
    }
}
