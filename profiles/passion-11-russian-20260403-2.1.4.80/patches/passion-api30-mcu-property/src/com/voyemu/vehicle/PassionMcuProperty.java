package com.voyemu.vehicle;

import android.util.SparseArray;

import com.mega.car.hal.IProperty;

import mega.car.hardware.CarPropertyConfig;
import mega.car.hardware.CarPropertyValue;
import mega.car.hardware.property.CarPropertyEvent;

/**
 * Private source candidate: the original service retains Binder permissions and client delivery.
 */
public final class PassionMcuProperty implements IProperty {
    public static final int SOC = 0x0500001d;
    private final IProperty original;
    private final String boot;
    private Receiver listener;
    private boolean subscribed;
    private boolean notifying;
    private boolean closed;
    private Snapshot snapshot;

    public static final class Snapshot {
        public final String boot, sourceSha256;
        public final long generation;
        public final int soc;
        public final boolean callbackReturned;

        private Snapshot(
                String boot, long generation, int soc, String sourceSha256, boolean returned) {
            this.boot = boot;
            this.generation = generation;
            this.soc = soc;
            this.sourceSha256 = sourceSha256;
            this.callbackReturned = returned;
        }
    }

    public PassionMcuProperty(IProperty original, String boot) {
        if (original == null
                || boot == null
                || !boot.matches("[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}")) {
            throw new IllegalArgumentException(
                    "PASSION_MCU: original transport and actual boot required");
        }

        this.original = original;
        this.boot = boot;
    }

    /** Only the protected in-process provider may call this after full host-state validation. */
    public synchronized Snapshot submit(
            String boot, long generation, int soc, String sourceSha256) {
        if (closed
                || !this.boot.equals(boot)
                || generation < 1
                || generation > 0xffffffffL
                || soc < 0
                || soc > 100
                || sourceSha256 == null
                || !sourceSha256.matches("[0-9a-f]{64}")) {
            throw new IllegalArgumentException("PASSION_MCU: boot, generation or SOC mismatch");
        }

        if (snapshot != null
                && (generation < snapshot.generation
                        || (generation == snapshot.generation
                                && (soc != snapshot.soc
                                        || !sourceSha256.equals(snapshot.sourceSha256))))) {
            throw new IllegalArgumentException("PASSION_MCU: stale or conflicting generation");
        }

        if (snapshot != null && generation == snapshot.generation && snapshot.callbackReturned) {
            return snapshot;
        }

        snapshot = new Snapshot(boot, generation, soc, sourceSha256, false);
        notifySoc();

        return snapshot;
    }

    public synchronized Snapshot status() {
        return snapshot;
    }

    private CarPropertyValue<Float> value() {
        return new CarPropertyValue<Float>(SOC, 0, Float.valueOf(snapshot.soc));
    }

    private void notifySoc() {
        if (closed || notifying || snapshot == null || listener == null || !subscribed) {
            return;
        }

        Snapshot pending = snapshot;
        // State is already readable if the original receiver synchronously reenters getProperty.
        notifying = true;

        try {
            listener.onPropertyChange(new CarPropertyEvent(0, value()));

            if (snapshot == pending) {
                snapshot =
                        new Snapshot(
                                boot, pending.generation, pending.soc, pending.sourceSha256, true);
            }
        } finally {
            notifying = false;
        }
    }

    @Override
    public synchronized CarPropertyValue getProperty(int property, int area) {
        if (property == SOC && area == 0 && snapshot != null) {
            return value();
        }

        return original.getProperty(property, area);
    }

    @Override
    public SparseArray<CarPropertyConfig<?>> getPropertyConfigMap() {
        SparseArray<CarPropertyConfig<?>> configs = original.getPropertyConfigMap();

        if (configs.get(SOC) == null) {
            throw new IllegalStateException("PASSION_MCU: original SOC config missing");
        }

        return configs;
    }

    @Override
    public synchronized void setListener(final Receiver receiver) {
        original.setListener(
                receiver == null
                        ? null
                        : new Receiver() {
                            @Override
                            public void onPropertyChange(java.util.List<CarPropertyEvent> events) {
                                for (CarPropertyEvent event : events) {
                                    onPropertyChange(event);
                                }
                            }

                            @Override
                            public void onPropertyChange(CarPropertyEvent event) {
                                if (event.getCarPropertyValue().getPropertyId() != SOC) {
                                    receiver.onPropertyChange(event);
                                }
                            }

                            @Override
                            public void onPropertySetError(int property, int area) {
                                if (property != SOC) {
                                    receiver.onPropertySetError(property, area);
                                }
                            }
                        });
        listener = receiver;
        notifySoc();
    }

    @Override
    public synchronized void subscribeProperty(int property, float rate) {
        if (property != SOC) {
            original.subscribeProperty(property, rate);

            return;
        }

        subscribed = true;
        notifySoc();
    }

    @Override
    public synchronized void unsubscribeProperty(int property) {
        if (property == SOC) {
            subscribed = false;
        } else {
            original.unsubscribeProperty(property);
        }
    }

    @Override
    public void setProperty(CarPropertyValue value) {
        if (value != null && value.getPropertyId() == SOC) {
            throw new UnsupportedOperationException("PASSION_MCU: SOC is manual-input read-only");
        }

        original.setProperty(value);
    }

    @Override
    public String getPermission(int property) {
        return original.getPermission(property);
    }

    @Override
    public float getSampleRate(int property) {
        return original.getSampleRate(property);
    }

    @Override
    public boolean enableDispatchInitValue() {
        return original.enableDispatchInitValue();
    }

    @Override
    public void init() {
        original.init();
    }

    @Override
    public synchronized void release() {
        closed = true;
        listener = null;
        subscribed = false;
        snapshot = null;
        original.release();
    }
}
