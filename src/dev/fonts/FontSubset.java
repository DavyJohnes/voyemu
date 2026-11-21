import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.TreeSet;

/**
 * Writes the TrueType subset of a variable glyf font that covers the CJK characters of the given UI
 * text files: CJK punctuation, unified ideographs and full-width forms. The subset holds the static
 * outlines and advances of the given wght axis value, the OS/2 and name tables of the font with
 * that weight class, and drops hinting, layout and variation tables, so the same inputs always give
 * the same bytes.
 *
 * <p>With --static it writes the subset of a static glyf font that covers Latin-1, the Russian
 * alphabet, typographic punctuation and every other non-CJK character of the text files. It keeps
 * the outlines as they are, with the components of composite glyphs, and drops the same tables.
 *
 * <p>Usage: java FontSubset.java FONT WEIGHT OUTPUT TEXT... or java FontSubset.java --static FONT
 * OUTPUT TEXT...
 */
public final class FontSubset {
    private FontSubset() {}

    static boolean cjk(int c) {
        return (c >= 0x3000 && c <= 0x303f)
                || (c >= 0x4e00 && c <= 0x9fff)
                || (c >= 0xff00 && c <= 0xffef);
    }

    /** Characters every static subset covers besides those of the text files. */
    static boolean base(int c) {
        return (c >= 0x20 && c <= 0x7e)
                || (c >= 0xa0 && c <= 0xff && c != 0xad)
                || c == 0x401
                || (c >= 0x410 && c <= 0x44f)
                || c == 0x451
                || (c >= 0x2013 && c <= 0x2014)
                || (c >= 0x2018 && c <= 0x2019)
                || (c >= 0x201c && c <= 0x201e)
                || c == 0x2022
                || c == 0x2026;
    }

    static Map<String, ByteBuffer> tables(byte[] font) {
        ByteBuffer data = ByteBuffer.wrap(font);
        int count = data.getShort(4) & 0xffff;
        Map<String, ByteBuffer> tables = new TreeMap<>();

        for (int i = 0; i < count; ++i) {
            int record = 12 + 16 * i;
            String tag = new String(font, record, 4, StandardCharsets.US_ASCII);
            int offset = data.getInt(record + 8), length = data.getInt(record + 12);
            tables.put(tag, ByteBuffer.wrap(font, offset, length).slice());
        }

        return tables;
    }

    /** Glyph of a character in a format 12 subtable: sequential map groups. */
    static int glyph12(ByteBuffer cmap, int format12, int c) {
        long groups = cmap.getInt(format12 + 12) & 0xffffffffL;

        for (int g = 0; g < groups; ++g) {
            int at = format12 + 16 + 12 * g;
            long start = cmap.getInt(at) & 0xffffffffL, end = cmap.getInt(at + 4) & 0xffffffffL;

            if (c >= start && c <= end) {
                return (int) (cmap.getInt(at + 8) + (c - start));
            }
        }

        return 0;
    }

    /** Glyph of each character from the Windows Unicode cmap, format 4 or 12. */
    static int glyph(ByteBuffer cmap, int c) {
        int count = cmap.getShort(2) & 0xffff, format4 = -1, format12 = -1;

        for (int i = 0; i < count; ++i) {
            int platform = cmap.getShort(4 + 8 * i) & 0xffff,
                    encoding = cmap.getShort(6 + 8 * i) & 0xffff,
                    offset = cmap.getInt(8 + 8 * i);

            if (platform == 3 && encoding == 10 && (cmap.getShort(offset) & 0xffff) == 12) {
                format12 = offset;
            } else if (platform == 3 && encoding == 1 && (cmap.getShort(offset) & 0xffff) == 4) {
                format4 = offset;
            }
        }

        if (format12 >= 0) {
            return glyph12(cmap, format12, c);
        }

        if (format4 < 0) {
            throw new IllegalStateException("FONT_SUBSET: Unicode cmap required");
        }

        int segments = (cmap.getShort(format4 + 6) & 0xffff) / 2;
        int ends = format4 + 14,
                starts = ends + 2 * segments + 2,
                deltas = starts + 2 * segments,
                ranges = deltas + 2 * segments;

        for (int s = 0; s < segments; ++s) {
            int end = cmap.getShort(ends + 2 * s) & 0xffff,
                    start = cmap.getShort(starts + 2 * s) & 0xffff;

            if (c < start || c > end) {
                continue;
            }

            int delta = cmap.getShort(deltas + 2 * s),
                    range = cmap.getShort(ranges + 2 * s) & 0xffff;

            if (range == 0) {
                return (c + delta) & 0xffff;
            }

            int value = cmap.getShort(ranges + 2 * s + range + 2 * (c - start)) & 0xffff;

            return value == 0 ? 0 : (value + delta) & 0xffff;
        }

        return 0;
    }

    static int offset(ByteBuffer loca, boolean wide, int glyph) {
        return wide ? loca.getInt(4 * glyph) : 2 * (loca.getShort(2 * glyph) & 0xffff);
    }

    static double f2dot14(ByteBuffer data, int at) {
        return data.getShort(at) / 16384.0;
    }

    static double roundF2dot14(double value) {
        return Math.round(value * 16384) / 16384.0;
    }

    /** Normalized coordinates of a weight: wght takes it through fvar and avar, other axes 0. */
    static double[] coordinates(Map<String, ByteBuffer> source, double weight) {
        ByteBuffer fvar = source.get("fvar");

        if (fvar == null || source.get("gvar") == null) {
            throw new IllegalStateException(
                    "FONT_SUBSET: variable font with fvar and gvar required");
        }

        int axes = fvar.getShort(4) & 0xffff, count = fvar.getShort(8) & 0xffff;
        double[] coordinates = new double[count];
        int wght = -1;

        for (int a = 0; a < count; ++a) {
            int at = axes + 20 * a;

            if (fvar.getInt(at) != 0x77676874) {
                continue;
            }

            double min = fvar.getInt(at + 4) / 65536.0,
                    initial = fvar.getInt(at + 8) / 65536.0,
                    max = fvar.getInt(at + 12) / 65536.0;

            if (weight < min || weight > max) {
                throw new IllegalArgumentException(
                        "FONT_SUBSET: weight " + weight + " outside " + min + ".." + max);
            }

            double value =
                    weight < initial
                            ? (weight - initial) / (initial - min)
                            : weight > initial ? (weight - initial) / (max - initial) : 0;
            coordinates[a] = roundF2dot14(value);
            wght = a;
        }

        if (wght < 0) {
            throw new IllegalStateException("FONT_SUBSET: font has no wght axis");
        }

        ByteBuffer avar = source.get("avar");

        if (avar != null) {
            coordinates[wght] = roundF2dot14(segmentMap(avar, wght, coordinates[wght]));
        }

        return coordinates;
    }

    /** The avar segment map of an axis: piecewise linear between its coordinate pairs. */
    static double segmentMap(ByteBuffer avar, int axis, double value) {
        int at = 8;

        for (int a = 0; a < axis; ++a) {
            at += 2 + 4 * (avar.getShort(at) & 0xffff);
        }

        int pairs = avar.getShort(at) & 0xffff;

        for (int p = 1; p < pairs; ++p) {
            int pair = at + 2 + 4 * p;
            double from = f2dot14(avar, pair), to = f2dot14(avar, pair + 2);
            double previousFrom = f2dot14(avar, pair - 4), previousTo = f2dot14(avar, pair - 2);

            if (value <= from) {
                return from == previousFrom
                        ? to
                        : previousTo
                                + (value - previousFrom)
                                        * (to - previousTo)
                                        / (from - previousFrom);
            }
        }

        return value;
    }

    /** Scalar of a variation tuple at the coordinates; intermediate is null without a region. */
    static double scalar(double[] coordinates, double[] peak, double[][] intermediate) {
        double result = 1;

        for (int a = 0; a < coordinates.length; ++a) {
            double p = peak[a], c = coordinates[a];
            double start = intermediate == null ? Math.min(p, 0) : intermediate[0][a];
            double end = intermediate == null ? Math.max(p, 0) : intermediate[1][a];

            if (p == 0 || start > p || p > end || (start < 0 && end > 0)) {
                continue;
            }

            if (c < start || c > end) {
                return 0;
            }

            if (c != p) {
                result *= c < p ? (c - start) / (p - start) : (end - c) / (end - p);
            }
        }

        return result;
    }

    /** Points of a simple glyph followed by its four phantom points. */
    record Outline(int[] ends, boolean[] on, double[] x, double[] y) {
        int points() {
            return x.length - 4;
        }
    }

    /** Reads a simple glyph (or an empty one) with the phantom points of its horizontal metrics. */
    static Outline outline(ByteBuffer glyf, int start, int end, int advance, int bearing) {
        int contours = end > start ? glyf.getShort(start) : 0;

        if (contours < 0) {
            throw new IllegalStateException("FONT_SUBSET: composite glyphs cannot be instanced");
        }

        int[] ends = new int[contours];

        for (int i = 0; i < contours; ++i) {
            ends[i] = glyf.getShort(start + 10 + 2 * i) & 0xffff;
        }

        int count = contours == 0 ? 0 : ends[contours - 1] + 1;
        boolean[] on = new boolean[count];
        double[] x = new double[count + 4], y = new double[count + 4];
        int at = contours == 0 ? start : start + 12 + 2 * contours;
        at += contours == 0 ? 0 : glyf.getShort(at - 2) & 0xffff;
        byte[] flags = new byte[count];

        for (int i = 0; i < count; ) {
            byte flag = glyf.get(at++);
            int repeat = (flag & 0x8) != 0 ? glyf.get(at++) & 0xff : 0;

            for (int r = 0; r <= repeat; ++r) {
                flags[i++] = flag;
            }
        }

        at = decode(glyf, at, flags, x, 0x2, 0x10);
        decode(glyf, at, flags, y, 0x4, 0x20);

        for (int i = 0; i < count; ++i) {
            on[i] = (flags[i] & 0x1) != 0;
        }

        double left = (contours == 0 ? 0 : glyf.getShort(start + 2)) - bearing;
        x[count] = left;
        x[count + 1] = left + advance;

        return new Outline(ends, on, x, y);
    }

    /** Decodes one axis of glyf coordinates and returns the position after them. */
    static int decode(ByteBuffer glyf, int at, byte[] flags, double[] out, int small, int same) {
        int value = 0;

        for (int i = 0; i < flags.length; ++i) {
            if ((flags[i] & small) != 0) {
                int delta = glyf.get(at++) & 0xff;
                value += (flags[i] & same) != 0 ? delta : -delta;
            } else if ((flags[i] & same) == 0) {
                value += glyf.getShort(at);
                at += 2;
            }

            out[i] = value;
        }

        return at;
    }

    /** Packed point numbers at a position; null means all points. Returns the end in next[0]. */
    static int[] packedPoints(ByteBuffer data, int at, int[] next) {
        int count = data.get(at++) & 0xff;

        if ((count & 0x80) != 0) {
            count = (count & 0x7f) << 8 | data.get(at++) & 0xff;
        }

        int[] points = count == 0 ? null : new int[count];

        for (int i = 0, point = 0; i < count; ) {
            int control = data.get(at++) & 0xff, run = (control & 0x7f) + 1;

            for (int r = 0; r < run && i < count; ++r) {
                point += (control & 0x80) != 0 ? data.getShort(at) & 0xffff : data.get(at) & 0xff;
                at += (control & 0x80) != 0 ? 2 : 1;
                points[i++] = point;
            }
        }

        next[0] = at;

        return points;
    }

    /** Packed deltas at a position. Returns the end in next[0]. */
    static int[] packedDeltas(ByteBuffer data, int at, int count, int[] next) {
        int[] deltas = new int[count];

        for (int i = 0; i < count; ) {
            int control = data.get(at++) & 0xff, run = (control & 0x3f) + 1;

            for (int r = 0; r < run && i < count; ++r) {
                if ((control & 0x80) != 0) {
                    deltas[i++] = 0;
                } else if ((control & 0x40) != 0) {
                    deltas[i++] = data.getShort(at);
                    at += 2;
                } else {
                    deltas[i++] = data.get(at++);
                }
            }
        }

        next[0] = at;

        return deltas;
    }

    /** Peak, or start and end, coordinates of a tuple record. */
    static double[] tuple(ByteBuffer data, int at, int axes) {
        double[] values = new double[axes];

        for (int a = 0; a < axes; ++a) {
            values[a] = f2dot14(data, at + 2 * a);
        }

        return values;
    }

    /** Adds the gvar deltas of a glyph at the coordinates to its outline. */
    static void vary(ByteBuffer gvar, int glyph, double[] coordinates, Outline outline) {
        int axes = gvar.getShort(4) & 0xffff;
        boolean wide = (gvar.getShort(14) & 0x1) != 0;
        int start =
                wide ? gvar.getInt(20 + 4 * glyph) : 2 * (gvar.getShort(20 + 2 * glyph) & 0xffff);
        int end = wide ? gvar.getInt(24 + 4 * glyph) : 2 * (gvar.getShort(22 + 2 * glyph) & 0xffff);

        if (end == start) {
            return;
        }

        int data = gvar.getInt(16) + start, shared = gvar.getInt(8);
        int header = data + 4, flags = gvar.getShort(data) & 0xffff;
        int[] next = {data + (gvar.getShort(data + 2) & 0xffff)};
        int[] sharedPoints = (flags & 0x8000) != 0 ? packedPoints(gvar, next[0], next) : null;
        int serialized = next[0];

        for (int t = 0; t < (flags & 0x0fff); ++t) {
            int size = gvar.getShort(header) & 0xffff, index = gvar.getShort(header + 2) & 0xffff;
            header += 4;
            double[] peak;

            if ((index & 0x8000) != 0) {
                peak = tuple(gvar, header, axes);
                header += 2 * axes;
            } else {
                peak = tuple(gvar, shared + 2 * axes * (index & 0x0fff), axes);
            }

            double[][] intermediate = null;

            if ((index & 0x4000) != 0) {
                intermediate =
                        new double[][] {
                            tuple(gvar, header, axes), tuple(gvar, header + 2 * axes, axes)
                        };
                header += 4 * axes;
            }

            double factor = scalar(coordinates, peak, intermediate);
            int body = serialized;
            serialized += size;

            if (factor != 0) {
                next[0] = body;
                int[] points =
                        (index & 0x2000) != 0 ? packedPoints(gvar, body, next) : sharedPoints;
                int count = points == null ? outline.x().length : points.length;
                int[] dx = packedDeltas(gvar, next[0], count, next);
                int[] dy = packedDeltas(gvar, next[0], count, next);
                apply(outline, points, dx, dy, factor);
            }
        }
    }

    /** Adds one scaled tuple; points without deltas follow their contour neighbours (IUP). */
    static void apply(Outline outline, int[] points, int[] dx, int[] dy, double factor) {
        int total = outline.x().length;
        double[] x = new double[total], y = new double[total];
        boolean[] touched = new boolean[total];

        for (int i = 0; i < dx.length; ++i) {
            int point = points == null ? i : points[i];

            if (point < total) {
                x[point] = dx[i];
                y[point] = dy[i];
                touched[point] = true;
            }
        }

        if (points != null) {
            int first = 0;

            for (int last : outline.ends()) {
                interpolate(outline, first, last, touched, x, y);
                first = last + 1;
            }
        }

        for (int i = 0; i < total; ++i) {
            outline.x()[i] += factor * x[i];
            outline.y()[i] += factor * y[i];
        }
    }

    /** Interpolates the deltas of the untouched points of one contour from touched neighbours. */
    static void interpolate(
            Outline outline, int first, int last, boolean[] touched, double[] x, double[] y) {
        List<Integer> anchors = new ArrayList<>();

        for (int i = first; i <= last; ++i) {
            if (touched[i]) {
                anchors.add(i);
            }
        }

        if (anchors.isEmpty() || anchors.size() == last - first + 1) {
            return;
        }

        for (int k = 0; k < anchors.size(); ++k) {
            int from = anchors.get(k), to = anchors.get((k + 1) % anchors.size());

            for (int i = from + 1; ; ++i) {
                int point = i > last ? i - (last - first + 1) : i;

                if (point == to) {
                    break;
                }

                x[point] = between(outline.x(), x, from, to, point);
                y[point] = between(outline.y(), y, from, to, point);
            }
        }
    }

    static double between(double[] original, double[] deltas, int a, int b, int point) {
        double ca = original[a], cb = original[b], da = deltas[a], db = deltas[b];

        if (ca == cb) {
            return da == db ? da : 0;
        }

        if (ca > cb) {
            double c = ca, d = da;
            ca = cb;
            da = db;
            cb = c;
            db = d;
        }

        double value = original[point];

        return value <= ca ? da : value >= cb ? db : da + (value - ca) * (db - da) / (cb - ca);
    }

    /** A simple glyph without instructions from rounded points; empty without contours. */
    static byte[] encode(Outline outline, int[] rounded) {
        int count = outline.points(), contours = outline.ends().length;

        if (contours == 0) {
            return new byte[0];
        }

        int xMin = Integer.MAX_VALUE, yMin = Integer.MAX_VALUE;
        int xMax = Integer.MIN_VALUE, yMax = Integer.MIN_VALUE;

        for (int i = 0; i < count; ++i) {
            xMin = Math.min(xMin, rounded[2 * i]);
            xMax = Math.max(xMax, rounded[2 * i]);
            yMin = Math.min(yMin, rounded[2 * i + 1]);
            yMax = Math.max(yMax, rounded[2 * i + 1]);
        }

        ByteBuffer out = ByteBuffer.allocate(12 + 2 * contours + 5 * count);
        out.putShort((short) contours).putShort((short) xMin).putShort((short) yMin);
        out.putShort((short) xMax).putShort((short) yMax);

        for (int last : outline.ends()) {
            out.putShort((short) last);
        }

        out.putShort((short) 0);

        // A coordinate delta is absent when zero, one byte with a sign flag below 256, else two.
        int[] deltas = new int[2 * count];

        for (int i = 0; i < count; ++i) {
            int flag = outline.on()[i] ? 0x1 : 0;

            for (int axis = 0; axis < 2; ++axis) {
                int delta = rounded[2 * i + axis] - (i == 0 ? 0 : rounded[2 * i - 2 + axis]);
                deltas[2 * i + axis] = delta;
                flag |= delta == 0 ? 0x10 << axis : Math.abs(delta) < 256 ? 0x2 << axis : 0;
                flag |= delta > 0 && Math.abs(delta) < 256 ? 0x10 << axis : 0;
            }

            out.put((byte) flag);
        }

        for (int axis = 0; axis < 2; ++axis) {
            for (int i = 0; i < count; ++i) {
                int delta = deltas[2 * i + axis];

                if (delta != 0 && Math.abs(delta) < 256) {
                    out.put((byte) Math.abs(delta));
                } else if (delta != 0) {
                    out.putShort((short) delta);
                }
            }
        }

        byte[] glyph = new byte[out.position()];
        out.flip().get(glyph);

        return glyph;
    }

    static byte[] bytes(ByteBuffer table) {
        byte[] out = new byte[table.remaining()];

        table.duplicate().get(out);

        return out;
    }

    static long checksum(byte[] table) {
        long sum = 0;

        for (int i = 0; i < table.length; i += 4) {
            long word = 0;

            for (int j = 0; j < 4; ++j) {
                word = (word << 8) | (i + j < table.length ? table[i + j] & 0xff : 0);
            }

            sum = (sum + word) & 0xffffffffL;
        }

        return sum;
    }

    static byte[] subset(byte[] font, double weight, TreeSet<Integer> characters) {
        Map<String, ByteBuffer> source = tables(font);
        ByteBuffer head = source.get("head"), loca = source.get("loca"), glyf = source.get("glyf");
        boolean wide = head.getShort(50) != 0;
        double[] coordinates = coordinates(source, weight);
        TreeMap<Integer, Integer> mapped = new TreeMap<>();
        TreeSet<Integer> glyphs = new TreeSet<>(List.of(0));

        for (int c : characters) {
            int g = glyph(source.get("cmap"), c);

            if (g == 0) {
                throw new IllegalStateException(
                        "FONT_SUBSET: font has no glyph for U+" + Integer.toHexString(c));
            }

            mapped.put(c, g);
            glyphs.add(g);
        }

        List<Integer> order = new ArrayList<>(glyphs);
        Map<Integer, Integer> renumbered = new TreeMap<>();

        for (int i = 0; i < order.size(); ++i) {
            renumbered.put(order.get(i), i);
        }

        ByteArrayOutputStream outlines = new ByteArrayOutputStream();
        ByteBuffer newLoca = ByteBuffer.allocate(4 * (order.size() + 1));
        ByteBuffer hmtx = source.get("hmtx");
        int metrics = source.get("hhea").getShort(34) & 0xffff;
        ByteBuffer newHmtx = ByteBuffer.allocate(4 * order.size());

        for (int g : order) {
            newLoca.putInt(outlines.size());
            int advance = hmtx.getShort(4 * Math.min(g, metrics - 1)) & 0xffff;
            short bearing =
                    g < metrics
                            ? hmtx.getShort(4 * g + 2)
                            : hmtx.getShort(4 * metrics + 2 * (g - metrics));
            Outline outline =
                    outline(
                            glyf,
                            offset(loca, wide, g),
                            offset(loca, wide, g + 1),
                            advance,
                            bearing);
            vary(source.get("gvar"), g, coordinates, outline);
            int[] rounded = new int[2 * outline.x().length];

            for (int i = 0; i < outline.x().length; ++i) {
                rounded[2 * i] = (int) Math.round(outline.x()[i]);
                rounded[2 * i + 1] = (int) Math.round(outline.y()[i]);
            }

            byte[] encoded = encode(outline, rounded);
            outlines.writeBytes(encoded);

            while (outlines.size() % 4 != 0) {
                outlines.write(0);
            }

            int left = rounded[2 * outline.points()], right = rounded[2 * outline.points() + 2];
            int xMin = encoded.length == 0 ? 0 : ByteBuffer.wrap(encoded).getShort(2);
            newHmtx.putShort((short) Math.max(0, right - left)).putShort((short) (xMin - left));
        }

        newLoca.putInt(outlines.size());
        TreeMap<String, byte[]> out = new TreeMap<>();
        byte[] newHead = bytes(head);
        ByteBuffer.wrap(newHead).putInt(8, 0).putShort(50, (short) 1);
        out.put("head", newHead);
        byte[] hhea = bytes(source.get("hhea"));
        ByteBuffer.wrap(hhea).putShort(34, (short) order.size());
        out.put("hhea", hhea);
        byte[] maxp = bytes(source.get("maxp"));
        ByteBuffer.wrap(maxp).putShort(4, (short) order.size());
        out.put("maxp", maxp);
        out.put("hmtx", newHmtx.array());
        out.put("loca", newLoca.array());
        out.put("glyf", outlines.toByteArray());
        out.put("cmap", cmap(mapped, renumbered));
        byte[] post = new byte[32];
        source.get("post").duplicate().get(post);
        ByteBuffer.wrap(post).putInt(0, 0x00030000);
        out.put("post", post);
        byte[] os2 = bytes(source.get("OS/2"));
        ByteBuffer.wrap(os2).putShort(4, (short) Math.round(weight));
        out.put("OS/2", os2);
        out.put("name", bytes(source.get("name")));

        return font(out);
    }

    /** Glyph index and its position in glyf of each component of a composite glyph. */
    static List<int[]> components(ByteBuffer glyf, int start, int end) {
        List<int[]> result = new ArrayList<>();

        if (end <= start || glyf.getShort(start) >= 0) {
            return result;
        }

        int at = start + 10, flags;

        do {
            flags = glyf.getShort(at) & 0xffff;
            result.add(new int[] {glyf.getShort(at + 2) & 0xffff, at + 2});
            at += 4 + ((flags & 0x1) != 0 ? 4 : 2);
            at += (flags & 0x8) != 0 ? 2 : (flags & 0x40) != 0 ? 4 : (flags & 0x80) != 0 ? 8 : 0;
        } while ((flags & 0x20) != 0);

        return result;
    }

    static byte[] subsetStatic(byte[] font, TreeSet<Integer> characters) {
        Map<String, ByteBuffer> source = tables(font);
        ByteBuffer head = source.get("head"), loca = source.get("loca"), glyf = source.get("glyf");

        if (source.containsKey("fvar")) {
            throw new IllegalStateException("FONT_SUBSET: static font required");
        }

        boolean wide = head.getShort(50) != 0;
        TreeMap<Integer, Integer> mapped = new TreeMap<>();
        TreeSet<Integer> glyphs = new TreeSet<>(List.of(0));

        for (int c : characters) {
            int g = glyph(source.get("cmap"), c);

            if (g == 0) {
                throw new IllegalStateException(
                        "FONT_SUBSET: font has no glyph for U+" + Integer.toHexString(c));
            }

            mapped.put(c, g);
            glyphs.add(g);
        }

        ArrayDeque<Integer> pending = new ArrayDeque<>(glyphs);

        while (!pending.isEmpty()) {
            int g = pending.pop();

            for (int[] component :
                    components(glyf, offset(loca, wide, g), offset(loca, wide, g + 1))) {
                if (glyphs.add(component[0])) {
                    pending.push(component[0]);
                }
            }
        }

        List<Integer> order = new ArrayList<>(glyphs);
        Map<Integer, Integer> renumbered = new TreeMap<>();

        for (int i = 0; i < order.size(); ++i) {
            renumbered.put(order.get(i), i);
        }

        ByteArrayOutputStream outlines = new ByteArrayOutputStream();
        ByteBuffer newLoca = ByteBuffer.allocate(4 * (order.size() + 1));
        ByteBuffer hmtx = source.get("hmtx");
        int metrics = source.get("hhea").getShort(34) & 0xffff;
        ByteBuffer newHmtx = ByteBuffer.allocate(4 * order.size());

        for (int g : order) {
            int start = offset(loca, wide, g), end = offset(loca, wide, g + 1);
            byte[] outline = new byte[end - start];
            glyf.duplicate().position(start).get(outline);

            for (int[] component : components(glyf, start, end)) {
                ByteBuffer.wrap(outline)
                        .putShort(component[1] - start, (short) (int) renumbered.get(component[0]));
            }

            newLoca.putInt(outlines.size());
            outlines.writeBytes(outline);

            while (outlines.size() % 4 != 0) {
                outlines.write(0);
            }

            newHmtx.putShort(hmtx.getShort(4 * Math.min(g, metrics - 1)))
                    .putShort(
                            g < metrics
                                    ? hmtx.getShort(4 * g + 2)
                                    : hmtx.getShort(4 * metrics + 2 * (g - metrics)));
        }

        newLoca.putInt(outlines.size());
        TreeMap<String, byte[]> out = new TreeMap<>();
        byte[] newHead = bytes(head);
        ByteBuffer.wrap(newHead).putInt(8, 0).putShort(50, (short) 1);
        out.put("head", newHead);
        byte[] hhea = bytes(source.get("hhea"));
        ByteBuffer.wrap(hhea).putShort(34, (short) order.size());
        out.put("hhea", hhea);
        byte[] maxp = bytes(source.get("maxp"));
        ByteBuffer.wrap(maxp).putShort(4, (short) order.size());
        out.put("maxp", maxp);
        out.put("hmtx", newHmtx.array());
        out.put("loca", newLoca.array());
        out.put("glyf", outlines.toByteArray());
        out.put("cmap", cmap(mapped, renumbered));
        byte[] post = new byte[32];
        source.get("post").duplicate().get(post);
        ByteBuffer.wrap(post).putInt(0, 0x00030000);
        out.put("post", post);
        out.put("OS/2", bytes(source.get("OS/2")));
        out.put("name", bytes(source.get("name")));

        return font(out);
    }

    /** A format 4 cmap with one segment per character. */
    static byte[] cmap(TreeMap<Integer, Integer> mapped, Map<Integer, Integer> renumbered) {
        int segments = mapped.size() + 1, length = 16 + 8 * segments;
        ByteBuffer table = ByteBuffer.allocate(12 + length);

        table.putShort((short) 0).putShort((short) 1);
        table.putShort((short) 3).putShort((short) 1).putInt(12);
        int search = Integer.highestOneBit(segments);
        table.putShort((short) 4).putShort((short) length).putShort((short) 0);
        table.putShort((short) (2 * segments)).putShort((short) (2 * search));
        table.putShort((short) Integer.numberOfTrailingZeros(search));
        table.putShort((short) (2 * (segments - search)));

        for (int c : mapped.keySet()) {
            table.putShort((short) c);
        }

        table.putShort((short) 0xffff).putShort((short) 0);

        for (int c : mapped.keySet()) {
            table.putShort((short) c);
        }

        table.putShort((short) 0xffff);

        for (var entry : mapped.entrySet()) {
            table.putShort((short) (renumbered.get(entry.getValue()) - entry.getKey()));
        }

        table.putShort((short) 1);

        for (int i = 0; i < segments; ++i) {
            table.putShort((short) 0);
        }

        return table.array();
    }

    static byte[] font(TreeMap<String, byte[]> tables) {
        int count = tables.size(), search = Integer.highestOneBit(count);
        ByteArrayOutputStream body = new ByteArrayOutputStream();
        ByteBuffer directory = ByteBuffer.allocate(12 + 16 * count);

        directory.putInt(0x00010000).putShort((short) count).putShort((short) (16 * search));
        directory.putShort((short) Integer.numberOfTrailingZeros(search));
        directory.putShort((short) (16 * (count - search)));
        int offset = 12 + 16 * count;

        for (var entry : tables.entrySet()) {
            byte[] table = entry.getValue();
            directory.put(entry.getKey().getBytes(StandardCharsets.US_ASCII));
            directory
                    .putInt((int) checksum(table))
                    .putInt(offset + body.size())
                    .putInt(table.length);
            body.writeBytes(table);

            while (body.size() % 4 != 0) {
                body.write(0);
            }
        }

        ByteArrayOutputStream result = new ByteArrayOutputStream();
        result.writeBytes(directory.array());
        result.writeBytes(body.toByteArray());
        byte[] font = result.toByteArray();
        int headOffset = 12 + 16 * new ArrayList<>(tables.keySet()).indexOf("head");
        int at = ByteBuffer.wrap(font).getInt(headOffset + 8);
        ByteBuffer.wrap(font).putInt(at + 8, (int) ((0xB1B0AFBAL - checksum(font)) & 0xffffffffL));

        return font;
    }

    public static void main(String[] args) throws IOException {
        if (args.length >= 3 && args[0].equals("--static")) {
            TreeSet<Integer> characters = new TreeSet<>();

            for (int c = 0x20; c <= 0xffff; ++c) {
                if (base(c)) {
                    characters.add(c);
                }
            }

            for (int i = 3; i < args.length; ++i) {
                Files.readString(Path.of(args[i]), StandardCharsets.UTF_8)
                        .codePoints()
                        .filter(c -> c >= 0x20 && !cjk(c))
                        .forEach(characters::add);
            }

            byte[] font = subsetStatic(Files.readAllBytes(Path.of(args[1])), characters);
            Files.write(Path.of(args[2]), font);
            System.out.println(
                    "FontSubset: " + characters.size() + " characters, " + font.length + " bytes");

            return;
        }

        if (args.length < 4) {
            throw new IllegalArgumentException(
                    "FONT WEIGHT OUTPUT TEXT... or --static FONT OUTPUT TEXT...");
        }

        TreeSet<Integer> characters = new TreeSet<>();

        for (int i = 3; i < args.length; ++i) {
            Files.readString(Path.of(args[i]), StandardCharsets.UTF_8)
                    .codePoints()
                    .filter(FontSubset::cjk)
                    .forEach(characters::add);
        }

        byte[] font =
                subset(
                        Files.readAllBytes(Path.of(args[0])),
                        Double.parseDouble(args[1]),
                        characters);
        Files.write(Path.of(args[2]), font);
        System.out.println(
                "FontSubset: " + characters.size() + " characters, " + font.length + " bytes");
    }
}
