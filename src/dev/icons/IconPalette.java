import java.awt.image.BufferedImage;
import java.awt.image.IndexColorModel;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import javax.imageio.ImageIO;

/**
 * Writes the application icons of src/assets from RGBA images. --png reduces an image to 256
 * palette entries with an alpha value each, the PNG colour type 3 with tRNS, by k-means over the
 * colours weighted by their alpha, so the soft edges and the shadow keep their alpha levels. --ico
 * writes an ICO file with each small image as a 32-bit DIB and the 256 px PNG as it is.
 *
 * <p>Usage: java IconPalette.java --png INPUT OUTPUT or java IconPalette.java --ico OUTPUT SMALL...
 * PNG256
 */
public final class IconPalette {
    private IconPalette() {}

    static double distance(double[] p, double[] q) {
        double sum = 0;

        for (int j = 0; j < 4; ++j) {
            sum += (p[j] - q[j]) * (p[j] - q[j]);
        }

        return sum;
    }

    static int channel(double value) {
        return (int) Math.max(0, Math.min(255, Math.round(value)));
    }

    static void palette(Path input, Path output) throws IOException {
        BufferedImage image = ImageIO.read(input.toFile());
        int width = image.getWidth(), height = image.getHeight(), count = width * height;
        double[][] pixels = new double[count][];

        for (int i = 0; i < count; ++i) {
            int c = image.getRGB(i % width, i / width), a = c >>> 24;
            double weight = a / 255.0;
            // Invisible colour costs no entries; alpha counts twice against the colour.
            pixels[i] =
                    new double[] {
                        ((c >> 16) & 255) * weight,
                        ((c >> 8) & 255) * weight,
                        (c & 255) * weight,
                        2.0 * a
                    };
        }

        int entries = 256;
        double[][] centers = new double[entries][];
        double[] nearest = new double[count];
        Arrays.fill(nearest, Double.MAX_VALUE);
        centers[0] = pixels[0].clone();

        // Farthest-point initialization keeps the result deterministic.
        for (int k = 1; k < entries; ++k) {
            int farthest = 0;

            for (int i = 0; i < count; ++i) {
                nearest[i] = Math.min(nearest[i], distance(pixels[i], centers[k - 1]));
                farthest = nearest[i] > nearest[farthest] ? i : farthest;
            }

            centers[k] = pixels[farthest].clone();
        }

        int[] labels = new int[count];

        for (int iteration = 0; iteration < 30; ++iteration) {
            double[][] sums = new double[entries][4];
            int[] members = new int[entries];

            for (int i = 0; i < count; ++i) {
                int best = 0;
                double bestDistance = Double.MAX_VALUE;

                for (int k = 0; k < entries; ++k) {
                    double d = distance(pixels[i], centers[k]);

                    if (d < bestDistance) {
                        bestDistance = d;
                        best = k;
                    }
                }

                labels[i] = best;
                ++members[best];

                for (int j = 0; j < 4; ++j) {
                    sums[best][j] += pixels[i][j];
                }
            }

            for (int k = 0; k < entries; ++k) {
                for (int j = 0; members[k] > 0 && j < 4; ++j) {
                    centers[k][j] = sums[k][j] / members[k];
                }
            }
        }

        byte[] r = new byte[entries], g = new byte[entries], b = new byte[entries];
        byte[] a = new byte[entries];

        for (int k = 0; k < entries; ++k) {
            double alpha = centers[k][3] / 2.0, weight = alpha / 255.0;
            a[k] = (byte) channel(alpha);
            r[k] = (byte) (weight > 0 ? channel(centers[k][0] / weight) : 0);
            g[k] = (byte) (weight > 0 ? channel(centers[k][1] / weight) : 0);
            b[k] = (byte) (weight > 0 ? channel(centers[k][2] / weight) : 0);
        }

        BufferedImage indexed =
                new BufferedImage(
                        width,
                        height,
                        BufferedImage.TYPE_BYTE_INDEXED,
                        new IndexColorModel(8, entries, r, g, b, a));

        for (int i = 0; i < count; ++i) {
            indexed.getRaster().setSample(i % width, i / width, 0, labels[i]);
        }

        ImageIO.write(indexed, "png", output.toFile());
    }

    /** A 32-bit bottom-up DIB with its AND mask, as an ICO entry holds it. */
    static byte[] dib(BufferedImage image) {
        int width = image.getWidth(), height = image.getHeight(), mask = (width + 31) / 32 * 4;
        ByteBuffer data =
                ByteBuffer.allocate(40 + 4 * width * height + mask * height)
                        .order(ByteOrder.LITTLE_ENDIAN);
        data.putInt(40).putInt(width).putInt(2 * height).putShort((short) 1).putShort((short) 32);
        data.putInt(0).putInt(4 * width * height + mask * height).putInt(0).putInt(0);
        data.putInt(0).putInt(0);

        for (int y = height - 1; y >= 0; --y) {
            for (int x = 0; x < width; ++x) {
                int c = image.getRGB(x, y);
                data.put((byte) c)
                        .put((byte) (c >> 8))
                        .put((byte) (c >> 16))
                        .put((byte) (c >>> 24));
            }
        }

        return data.array();
    }

    static void icon(Path output, List<Path> inputs) throws IOException {
        List<byte[]> images = new ArrayList<>();
        List<Integer> sizes = new ArrayList<>();

        for (int i = 0; i < inputs.size(); ++i) {
            BufferedImage image = ImageIO.read(inputs.get(i).toFile());
            boolean last = i == inputs.size() - 1;

            if (last != (image.getWidth() == 256) || image.getWidth() != image.getHeight()) {
                throw new IllegalArgumentException(
                        "ICON_PALETTE: square small images followed by the 256 px PNG required");
            }

            images.add(last ? Files.readAllBytes(inputs.get(i)) : dib(image));
            sizes.add(image.getWidth());
        }

        ByteBuffer directory =
                ByteBuffer.allocate(6 + 16 * images.size()).order(ByteOrder.LITTLE_ENDIAN);
        directory.putShort((short) 0).putShort((short) 1).putShort((short) images.size());
        int offset = 6 + 16 * images.size();
        ByteArrayOutputStream body = new ByteArrayOutputStream();

        for (int i = 0; i < images.size(); ++i) {
            int size = sizes.get(i) % 256;
            directory.put((byte) size).put((byte) size).put((byte) 0).put((byte) 0);
            directory.putShort((short) 1).putShort((short) 32);
            directory.putInt(images.get(i).length).putInt(offset + body.size());
            body.writeBytes(images.get(i));
        }

        ByteArrayOutputStream result = new ByteArrayOutputStream();
        result.writeBytes(directory.array());
        result.writeBytes(body.toByteArray());
        Files.write(output, result.toByteArray());
    }

    public static void main(String[] args) throws IOException {
        if (args.length == 3 && args[0].equals("--png")) {
            palette(Path.of(args[1]), Path.of(args[2]));
        } else if (args.length >= 3 && args[0].equals("--ico")) {
            List<Path> inputs = new ArrayList<>();

            for (int i = 2; i < args.length; ++i) {
                inputs.add(Path.of(args[i]));
            }

            icon(Path.of(args[1]), inputs);
        } else {
            throw new IllegalArgumentException(
                    "--png INPUT OUTPUT or --ico OUTPUT SMALL... PNG256");
        }
    }
}
