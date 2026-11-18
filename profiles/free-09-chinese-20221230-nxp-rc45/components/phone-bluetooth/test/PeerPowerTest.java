package com.qinggan.bluetooth.internal.ivt8;

/**
 * Exercises the real NXP power authority and synthetic call/contact state without Android stubs.
 */
public final class PeerPowerTest {
    private static int checks;

    private static void check(boolean value) {
        checks++;

        if (!value) {
            throw new AssertionError("check " + checks);
        }
    }

    private static final class Store implements PeerPower.Store {
        boolean value = true, failRead, failWrite;
        int writes;

        public boolean read() throws java.io.IOException {
            if (failRead) {
                throw new java.io.IOException("read");
            }

            return value;
        }

        public void write(boolean next) throws java.io.IOException {
            if (failWrite) {
                throw new java.io.IOException("write");
            }

            value = next;
            writes++;
        }
    }

    public static void main(String[] args) throws Exception {
        Store store = new Store();
        SyntheticPhonePeer peer = new SyntheticPhonePeer();
        PeerPower power = new PeerPower(store, peer);

        check(power.enabled());
        check(peer.contacts().size() == 2);
        check(!power.set(true) && store.writes == 0);
        peer.connect();
        peer.incoming("+70000000001");
        check(peer.state() == SyntheticPhonePeer.INCOMING);
        check(power.set(false));
        check(
                !power.enabled()
                        && !peer.connected()
                        && peer.state() == SyntheticPhonePeer.TERMINATED
                        && peer.number().isEmpty());
        check(store.writes == 1 && !store.value);
        check(!power.set(false) && store.writes == 1);

        try {
            power.requireEnabled();
            throw new AssertionError("off accepted");
        } catch (IllegalStateException expected) {
            check(true);
        }

        PeerPower restored = new PeerPower(store, new SyntheticPhonePeer());
        check(!restored.enabled());
        check(power.set(true));
        power.requireEnabled();
        check(store.value && store.writes == 2);
        peer.connect();
        peer.dial("+70000000002");
        peer.answer();
        check(peer.state() == SyntheticPhonePeer.ACTIVE);
        store.failWrite = true;

        try {
            power.set(false);
            throw new AssertionError("failed persistence accepted");
        } catch (java.io.IOException expected) {
            check(
                    power.enabled()
                            && peer.connected()
                            && peer.state() == SyntheticPhonePeer.ACTIVE
                            && store.value);
        }

        store.failWrite = false;
        check(power.set(false));
        check(peer.state() == SyntheticPhonePeer.TERMINATED);
        check(
                peer.contacts().get(0).number.equals("+70000000001")
                        && peer.contacts().get(1).number.equals("+70000000002"));
        store.failRead = true;

        try {
            new PeerPower(store, new SyntheticPhonePeer());
            throw new AssertionError("invalid persistence accepted");
        } catch (java.io.IOException expected) {
            check(true);
        }

        for (String invalid :
                new String[] {"70000000001", "+17000000001", "+7", "+70000000001 ", "+7bad"}) {
            try {
                new SyntheticPhonePeer.Contact("Test", invalid);
                throw new AssertionError("invalid contact accepted");
            } catch (IllegalArgumentException expected) {
                check(true);
            }
        }

        System.out.println("NXP PeerPowerTest PASS " + checks);
    }
}
