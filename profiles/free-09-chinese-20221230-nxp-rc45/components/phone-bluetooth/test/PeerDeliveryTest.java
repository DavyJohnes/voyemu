package com.qinggan.bluetooth.internal.ivt8;

import java.util.Arrays;

public final class PeerDeliveryTest {
    public static void main(String[] args) throws Exception {
        SyntheticPhonePeer peer = new SyntheticPhonePeer();
        final boolean[] disk = {true};
        PeerPower power =
                new PeerPower(
                        new PeerPower.Store() {
                            public boolean read() {
                                return disk[0];
                            }

                            public void write(boolean value) {
                                disk[0] = value;
                            }
                        },
                        peer);

        peer.connect();
        peer.incoming("+70000000001");

        if (!power.set(false)) {
            throw new AssertionError();
        }

        int[] failures = {0}, healthy = {0}, published = {0};
        PeerDelivery.each(
                Arrays.asList(
                        (Runnable)
                                () -> {
                                    throw new UnsupportedOperationException();
                                },
                        (Runnable)
                                () -> {
                                    if (power.enabled()
                                            || peer.connected()
                                            || peer.state() != SyntheticPhonePeer.TERMINATED) {
                                        throw new AssertionError();
                                    }

                                    healthy[0]++;
                                }),
                Runnable::run,
                () -> failures[0]++);

        if (power.enabled() || disk[0] || peer.connected()) {
            throw new AssertionError();
        }

        published[0]++;

        if (power.set(false) || failures[0] != 1 || healthy[0] != 1 || published[0] != 1) {
            throw new AssertionError();
        }

        System.out.println(
                "NXP PeerDeliveryTest PASS throwing-first listener, later listener sees committed"
                        + " OFF, publication remains reachable, no replay");
    }
}
