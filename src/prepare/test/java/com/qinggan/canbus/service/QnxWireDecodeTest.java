package com.qinggan.canbus.service;

import java.nio.charset.StandardCharsets;

/** Independent semantic oracle from Free A11 ivcd subtype and field reads, not OEM code. */
public final class QnxWireDecodeTest {
    static final class Decoded {
        int state, vehicle, type, duration;
        String text;
    }

    static Decoded decode(byte[] b) {
        if (b.length != 77 || b[5] != 0x6a || b[14] != 1) {
            throw new AssertionError();
        }

        Decoded d = new Decoded();
        d.state = b[6] & 255;
        d.vehicle = b[7] & 255;
        d.type = b[12] & 255;
        int n = b[13] & 255;

        if (d.type == 1 || d.type == 2) {
            if (n > 60) {
                throw new AssertionError();
            }

            d.text = new String(b, 15, n, StandardCharsets.UTF_8);
        } else if (d.type == 3) {
            if (n != 3) {
                throw new AssertionError();
            }

            d.duration = ((b[15] & 255) << 16) | ((b[16] & 255) << 8) | (b[17] & 255);
        } else {
            throw new AssertionError();
        }

        return d;
    }

    static void check(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    public static void main(String[] args) {
        Decoded n = decode(IpkFrames.phone("GOING_CALL", 0, 0, null, "+79990000001")[0]);

        check(n.type == 2 && n.state == 1 && n.vehicle == 2 && n.text.equals("+79990000001"));
        Decoded c = decode(IpkFrames.phone("COMING_CALL", 0, 0, "Иван Петров", "+79990000001")[0]);
        check(c.type == 1 && c.state == 2 && c.vehicle == 2 && c.text.equals("Иван Петров"));
        byte[][] active = IpkFrames.phone("CONNECTED", 0, 0x123456, "Иван Петров", "+79990000001");
        Decoded d = decode(active[1]);
        check(d.state == 4 && d.type == 3 && d.duration == 0x123456);
        Decoded clear = decode(IpkFrames.phone("HANG_UP", 0, 0, null, null)[0]);
        check(clear.state == 3 && clear.duration == 0);
        System.out.println("QnxWireDecodeTest PASS");
    }
}
