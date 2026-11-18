package com.qinggan.bluetooth.internal.ivt8;

/** NXP Phone main-looper power authority. Persistence precedes every observable transition. */
final class PeerPower {
    interface Store {
        boolean read() throws java.io.IOException;

        void write(boolean enabled) throws java.io.IOException;
    }

    private final Store store;
    private final SyntheticPhonePeer peer;
    private volatile boolean enabled;

    PeerPower(Store store, SyntheticPhonePeer peer) throws java.io.IOException {
        if (store == null || peer == null) {
            throw new IllegalArgumentException("PHONE_POWER_ARGUMENT");
        }

        this.store = store;
        this.peer = peer;
        enabled = store.read();

        if (!enabled && peer.connected()) {
            peer.disconnect();
        }
    }

    boolean enabled() {
        return enabled;
    }

    boolean set(boolean next) throws java.io.IOException {
        if (next == enabled) {
            return false;
        }

        store.write(next);

        if (!next && peer.connected()) {
            peer.disconnect();
        }

        enabled = next;
        peer.record(next ? "power_on" : "power_off");

        return true;
    }

    void requireEnabled() {
        if (!enabled) {
            throw new IllegalStateException("PHONE_POWER_OFF");
        }
    }
}
