package com.qinggan.canbus.service;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/**
 * Owned encoders of the IVC frames that the OEM head unit sends to the cluster ivcd, following the
 * byte layout of the OEM InstrumentInteractionComponent and VoyahInstrumentClusterInfoSync: {@code
 * 0A 10 00 len_hi len_lo cmd payload 00 0D}, where len counts cmd and payload. Text keeps the OEM
 * byte limits but is truncated on a UTF-8 character boundary.
 */
final class IpkFrames {
    static final int HEARTBEAT = 0x67;
    static final int TBT = 0x68;
    static final int PHONE = 0x6a;
    static final int MEDIA = 0x6e;
    static final int RADIO = 0x6f;
    static final int LANES = 0x76;
    static final int ROAD = 0x77;
    private static final int PART = 60;

    private IpkFrames() {}

    /** Frame with header and tail; the payload starts at index 6. */
    static byte[] frame(int cmd, int payload) {
        int length = payload + 1;
        byte[] frame = new byte[length + 7];

        frame[0] = 10;
        frame[1] = 16;
        frame[3] = (byte) (length >>> 8);
        frame[4] = (byte) length;
        frame[5] = (byte) cmd;
        frame[length + 6] = 13;

        return frame;
    }

    /** Command of a frame with the IVC header and tail, or -1. */
    static int command(byte[] frame) {
        if (frame == null || frame.length < 9 || frame[0] != 10 || frame[frame.length - 1] != 13) {
            return -1;
        }

        int length = ((frame[3] & 255) << 8) | (frame[4] & 255);

        return length + 7 == frame.length ? frame[5] & 255 : -1;
    }

    static byte[] heartbeat() {
        byte[] frame = frame(HEARTBEAT, 1);

        frame[6] = 1;

        return frame;
    }

    /** UTF-8 bytes of text limited to limit bytes without splitting a character. */
    static byte[] utf8(String text, int limit) {
        byte[] bytes = text == null ? new byte[0] : text.getBytes(StandardCharsets.UTF_8);
        int length = Math.min(bytes.length, limit);

        if (length < bytes.length) {
            while (length > 0 && (bytes[length] & 0xc0) == 0x80) {
                --length;
            }
        }

        return Arrays.copyOf(bytes, length);
    }

    /** One 77-byte text frame per 60-byte part: [12] field, [13] total length, [14] part. */
    private static byte[][] text(byte[] base, int field, byte[] text) {
        int parts = Math.max(1, (text.length + PART - 1) / PART);
        byte[][] result = new byte[parts][];

        for (int part = 0; part < parts; part++) {
            byte[] frame = base.clone();
            int offset = part * PART;

            frame[12] = (byte) field;
            frame[13] = (byte) text.length;
            frame[14] = (byte) (part + 1);
            System.arraycopy(text, offset, frame, 15, Math.min(PART, text.length - offset));
            result[part] = frame;
        }

        return result;
    }

    private static byte[][] concat(byte[][] first, byte[][] second) {
        byte[][] result = Arrays.copyOf(first, first.length + second.length);

        System.arraycopy(second, 0, result, first.length, second.length);

        return result;
    }

    // ------------------------------------------------------------------
    // 0x6a phone
    // ------------------------------------------------------------------

    private static int phoneState(String state) {
        if ("GOING_CALL".equals(state)) {
            return 1;
        } else if ("COMING_CALL".equals(state)) {
            return 2;
        } else if ("NOTHING".equals(state) || "HANG_UP".equals(state)) {
            return 3;
        } else if ("CONNECTED".equals(state)) {
            return 4;
        } else if ("HOLD".equals(state)) {
            return 5;
        }

        throw new IllegalArgumentException("phone state");
    }

    private static void validate(String text) {
        if (text == null) {
            return;
        }

        if (text.length() > 1024 || text.indexOf('\0') >= 0) {
            throw new IllegalArgumentException("phone text");
        }

        for (int i = 0; i < text.length(); ++i) {
            char value = text.charAt(i);

            if (Character.isHighSurrogate(value)) {
                if (++i >= text.length() || !Character.isLowSurrogate(text.charAt(i))) {
                    throw new IllegalArgumentException("phone text encoding");
                }
            } else if (Character.isLowSurrogate(value)) {
                throw new IllegalArgumentException("phone text encoding");
            }
        }
    }

    /**
     * Name frame (type 1) or number frame (type 2) of at most 60 bytes, and the duration frame
     * (type 3, 24-bit seconds) for the CONNECTED and ended states.
     */
    static byte[][] phone(String state, int vehicleCall, int duration, String name, String number) {
        final int wire = phoneState(state);

        if (vehicleCall < 0 || vehicleCall > 1 || duration < 0 || duration > 0xffffff) {
            throw new IllegalArgumentException("phone values");
        }

        validate(name);
        validate(number);
        boolean named = name != null && !name.isEmpty();
        byte[] text = utf8(named ? name : number, PART);
        byte[] base = frame(PHONE, 69);
        base[6] = (byte) wire;
        base[7] = (byte) (vehicleCall == 1 ? 1 : 2);
        byte[][] result = text.length == 0 ? new byte[0][] : text(base, named ? 1 : 2, text);

        if (wire == 3 || wire == 4) {
            byte[] frame = base.clone();
            frame[12] = 3;
            frame[13] = 3;
            frame[14] = 1;
            frame[15] = (byte) (duration >>> 16);
            frame[16] = (byte) (duration >>> 8);
            frame[17] = (byte) duration;
            result = concat(result, new byte[][] {frame});
        }

        return result;
    }

    // ------------------------------------------------------------------
    // 0x6e media and 0x6f radio
    // ------------------------------------------------------------------

    /**
     * Song frames (field 1, only for a known song) and artist frames (field 2, a space without an
     * artist), each up to 120 bytes in two parts; [6] source, [7] device, [8] content.
     */
    static byte[][] media(int source, int device, int content, String song, String artist) {
        byte[] base = frame(MEDIA, 69);

        base[6] = (byte) source;
        base[7] = (byte) device;
        base[8] = (byte) content;
        byte[][] songs = song == null ? new byte[0][] : text(base, 1, utf8(song, 2 * PART));

        return concat(
                songs,
                text(base, 2, utf8(artist == null || artist.isEmpty() ? " " : artist, 2 * PART)));
    }

    /**
     * Station frames (field 2 frequency or 5 name, up to 180 bytes in three parts) and, with an
     * album, album frames (field 1, up to 120 bytes); [6] band, [7] search state.
     */
    static byte[][] radio(int band, int search, int field, String name, String album) {
        byte[] base = frame(RADIO, 69);

        base[6] = (byte) band;
        base[7] = (byte) search;
        byte[][] result = text(base, field, utf8(name, 3 * PART));

        return album == null ? result : concat(result, text(base, 1, utf8(album, 2 * PART)));
    }

    // ------------------------------------------------------------------
    // 0x68 turn by turn, 0x76 lanes, 0x77 current road
    // ------------------------------------------------------------------

    /** Guidance: [6]=1, [7..8] icon, [11..13] field lengths, distance, unit and road from 15. */
    static byte[] guide(int icon, String distance, String unit, String road) {
        byte[] d = utf8(distance, 255), u = utf8(unit, 255), r = utf8(road, 255);
        byte[] frame = frame(TBT, 9 + d.length + u.length + r.length);

        frame[6] = 1;
        frame[7] = (byte) (icon >>> 8);
        frame[8] = (byte) icon;
        frame[11] = (byte) d.length;
        frame[12] = (byte) u.length;
        frame[13] = (byte) r.length;
        System.arraycopy(d, 0, frame, 15, d.length);
        System.arraycopy(u, 0, frame, 15 + d.length, u.length);
        System.arraycopy(r, 0, frame, 15 + d.length + u.length, r.length);

        return frame;
    }

    static byte[] guideOff() {
        byte[] frame = frame(TBT, 1);

        frame[6] = 2;

        return frame;
    }

    /** Lane frame: [6] count, then 16-bit lane ids; no ids hides the lanes. */
    static byte[] lanes(int[] ids) {
        int count = Math.min(ids.length, 255);
        byte[] frame = frame(LANES, count == 0 ? 1 : 1 + 2 * count);

        frame[6] = (byte) count;

        for (int i = 0; i < count; i++) {
            frame[7 + 2 * i] = (byte) (ids[i] >>> 8);
            frame[8 + 2 * i] = (byte) ids[i];
        }

        return frame;
    }

    /** Current road while roaming: [6]=1, [13] length, name from 15; null for no name. */
    static byte[] road(String name) {
        byte[] r = utf8(name, 255);

        if (r.length == 0) {
            return null;
        }

        byte[] frame = frame(ROAD, 9 + r.length);
        frame[6] = 1;
        frame[13] = (byte) r.length;
        System.arraycopy(r, 0, frame, 15, r.length);

        return frame;
    }

    static byte[] roadOff() {
        byte[] frame = frame(ROAD, 1);

        frame[6] = 2;

        return frame;
    }

    // ------------------------------------------------------------------
    // Presentation readiness of VoyahInstrumentCluster, 20-byte frames
    // ------------------------------------------------------------------

    /** syncReadyState: 0x60 NAVI, 0x61 MEDIA, 0x63 PHONE, 0x7a REST, 0x7d CAMP; null otherwise. */
    static byte[] ready(String category, int state) {
        byte[] frame = frame(0, 12);

        switch (category == null ? "" : category) {
            case "qg.car.cluster.NAVI":
                frame[5] = 0x60;
                frame[7] = (byte) (state == 1 ? 2 : 1);
                break;
            case "qg.car.cluster.MEDIA":
            case "qg.car.cluster.PHONE":
                frame[5] = (byte) ("qg.car.cluster.MEDIA".equals(category) ? 0x61 : 0x63);
                frame[10] = (byte) (state == 1 ? 2 : state == 2 ? 3 : 1);
                break;
            case "qg.car.cluster.REST":
            case "qg.car.cluster.CAMP":
                frame[5] = (byte) ("qg.car.cluster.REST".equals(category) ? 0x7a : 0x7d);
                frame[6] = (byte) (state == 1 ? 1 : state == 2 ? 2 : 0);
                break;
            default:
                return null;
        }

        return frame;
    }

    private static int naviView(String category) {
        switch (category) {
            case "qg.car.cluster.NAVI_SMALL":
                return 1;
            case "qg.car.cluster.NVS":
                return 2;
            case "qg.car.cluster.NAVI_AR":
                return 3;
            case "qg.car.cluster.NAVI_FULL":
                return 4;
            default:
                return 0;
        }
    }

    /** requestPresentation: [6] switch 1 or 2; null for an unknown category. */
    static byte[] presentation(String category, int value) {
        String name = category == null ? "" : category;
        byte[] frame = frame(0, 12);
        int view = naviView(name);

        frame[6] = (byte) (value == 1 || value == 2 ? value : 0);

        if (view != 0) {
            frame[5] = 0x60;
            frame[7] = 2;
            frame[8] = (byte) view;
        } else if ("qg.car.cluster.MEDIA_SMALL".equals(name)) {
            frame[5] = 0x61;
        } else if ("qg.car.cluster.PHONE".equals(name)) {
            frame[5] = 0x63;
        } else if ("qg.car.cluster.REST".equals(name)) {
            frame[5] = 0x7b;
        } else if ("qg.car.cluster.CAMP".equals(name)) {
            frame[5] = 0x7e;
        } else {
            return null;
        }

        return frame;
    }

    /** notifyReadyToShow; navigation views echo the sequence of the cluster request in [2]. */
    static byte[] readyToShow(String category, int sequence) {
        String name = category == null ? "" : category;
        byte[] frame = frame(0, 12);
        int view = naviView(name);

        if (view != 0 || "qg.car.cluster.FAVORITE".equals(name)) {
            frame[2] = (byte) sequence;
            frame[5] = 0x60;

            if (view != 0) {
                frame[8] = (byte) view;
                frame[11] = 1;
            } else {
                frame[9] = 1;
                frame[11] = 2;
            }
        } else if ("qg.car.cluster.MEDIA_SMALL".equals(name)) {
            frame[5] = 0x61;
            frame[11] = 1;
        } else if ("qg.car.cluster.CONTACTS".equals(name)) {
            frame[5] = 0x63;
            frame[11] = 1;
        } else if ("qg.car.cluster.REST".equals(name)) {
            frame[5] = 0x7c;
        } else if ("qg.car.cluster.CAMP".equals(name)) {
            frame[5] = 0x7f;
        } else {
            return null;
        }

        return frame;
    }
}
