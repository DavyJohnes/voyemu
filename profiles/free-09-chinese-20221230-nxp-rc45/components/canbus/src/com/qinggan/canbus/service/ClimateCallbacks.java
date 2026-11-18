package com.qinggan.canbus.service;

/** Dispatch immutable climate snapshots after the Binder method has released its monitor. */
final class ClimateCallbacks<T> {
    interface Dispatcher {
        void post(Runnable action);
    }

    interface Delivery<T> {
        void send(T snapshot);
    }

    private final Dispatcher dispatcher;

    ClimateCallbacks(Dispatcher dispatcher) {
        this.dispatcher = dispatcher;
    }

    void enqueue(T snapshot, Delivery<T> delivery) {
        dispatcher.post(() -> delivery.send(snapshot));
    }
}
