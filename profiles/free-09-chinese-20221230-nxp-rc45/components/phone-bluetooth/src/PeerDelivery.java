package com.qinggan.bluetooth.internal.ivt8;

/** One OEM listener must not suppress another listener or publication of committed state. */
final class PeerDelivery {
    static <T> void each(
            Iterable<T> consumers, java.util.function.Consumer<T> notify, Runnable failed) {
        for (T consumer : consumers) {
            try {
                notify.accept(consumer);
            } catch (RuntimeException failure) {
                // checkstyle-allow IllegalCatch: one failing OEM listener never stops the others
                failed.run();
            }
        }
    }
}
