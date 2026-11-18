package com.voyemu.vehicle;

import android.util.SparseArray;

import com.mega.car.hal.IProperty;

import mega.car.hardware.CarPropertyConfig;
import mega.car.hardware.CarPropertyValue;
import mega.car.hardware.property.CarPropertyEvent;

public final class PassionMcuPropertyTest {
    private static final String BOOT = "12345678-1234-1234-1234-123456789abc";
    private static final String SHA =
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    private static final int SOC = PassionMcuProperty.SOC;

    private static final class Original implements IProperty {
        int reads, writes, subscriptions, errors, events, releases;

        @Override
        public boolean enableDispatchInitValue() {
            return true;
        }

        @Override
        public float getSampleRate(int property) {
            return -1;
        }

        @Override
        public void init() {}

        Receiver receiver;

        @Override
        public CarPropertyValue getProperty(int property, int area) {
            reads++;

            return null;
        }

        @Override
        public SparseArray<CarPropertyConfig<?>> getPropertyConfigMap() {
            return new SparseArray<>();
        }

        @Override
        public void setListener(Receiver receiver) {
            this.receiver = receiver;
        }

        @Override
        public void subscribeProperty(int property, float rate) {
            subscriptions++;
        }

        @Override
        public void unsubscribeProperty(int property) {
            subscriptions--;
        }

        @Override
        public void setProperty(CarPropertyValue value) {
            writes++;
        }

        @Override
        public String getPermission(int property) {
            return "original-permission";
        }

        @Override
        public void release() {
            releases++;
        }
    }

    private static void rejects(Runnable action) {
        try {
            action.run();
        } catch (IllegalArgumentException
                | IllegalStateException
                | UnsupportedOperationException expected) {
            return;
        }

        throw new AssertionError("operation unexpectedly accepted");
    }

    public static void main(String[] args) {
        boolean assertions = false;

        assert assertions = true;

        if (!assertions) {
            throw new AssertionError("run with -ea");
        }

        Original original = new Original();
        PassionMcuProperty bridge = new PassionMcuProperty(original, BOOT);
        assert bridge.status() == null;
        assert bridge.getProperty(SOC, 0)
                == null; // Never invent the fixture SOC before host submission.
        rejects(() -> bridge.getPropertyConfigMap()); // No fabricated OEM config.
        rejects(() -> bridge.submit(BOOT, 0, 55, SHA));
        rejects(() -> bridge.submit(BOOT, 1, 101, SHA));
        rejects(() -> bridge.submit("22345678-1234-1234-1234-123456789abc", 1, 55, SHA));
        assert bridge.status() == null;
        assert !bridge.submit(BOOT, 1, 55, SHA).callbackReturned;
        assert bridge.getProperty(SOC, 0).getValue().equals(Float.valueOf(55));
        assert bridge.getProperty(SOC, 1) == null;
        final int[] callbacks = {0};
        bridge.setListener(
                new IProperty.Receiver() {
                    @Override
                    public void onPropertyChange(java.util.List<CarPropertyEvent> events) {
                        for (CarPropertyEvent event : events) {
                            onPropertyChange(event);
                        }
                    }

                    @Override
                    public void onPropertyChange(CarPropertyEvent event) {
                        callbacks[0]++;
                        assert event.getCarPropertyValue()
                                .getValue()
                                .equals(bridge.getProperty(SOC, 0).getValue());
                    }

                    @Override
                    public void onPropertySetError(int property, int area) {
                        original.errors++;
                    }
                });
        bridge.subscribeProperty(SOC, 0);
        assert callbacks[0] == 1 && bridge.status().callbackReturned;
        assert bridge.submit(BOOT, 1, 55, SHA).callbackReturned && callbacks[0] == 1;
        assert bridge.submit(BOOT, 2, 61, SHA).callbackReturned && callbacks[0] == 2;
        assert bridge.getProperty(SOC, 0).getValue().equals(Float.valueOf(61));
        rejects(() -> bridge.submit(BOOT, 1, 55, SHA));
        rejects(() -> bridge.submit(BOOT, 2, 55, SHA));
        assert bridge.status().generation == 2 && bridge.status().soc == 61;
        int target = 0x05000007;
        bridge.getProperty(target, 0);
        bridge.setProperty(new CarPropertyValue<Integer>(target, 0, Integer.valueOf(80)));
        assert original.writes == 1;
        rejects(() -> bridge.setProperty(new CarPropertyValue<Float>(SOC, 0, Float.valueOf(80))));
        bridge.unsubscribeProperty(SOC);
        bridge.submit(BOOT, 3, 0, SHA);
        assert !bridge.status().callbackReturned && callbacks[0] == 2;
        bridge.subscribeProperty(SOC, 0);
        assert bridge.status().callbackReturned && callbacks[0] == 3;
        bridge.release();
        assert original.releases == 1 && bridge.status() == null;
        rejects(() -> bridge.submit(BOOT, 4, 100, SHA));

        Original failingOriginal = new Original();
        PassionMcuProperty retry = new PassionMcuProperty(failingOriginal, BOOT);
        final boolean[] fail = {true};
        retry.setListener(
                new IProperty.Receiver() {
                    @Override
                    public void onPropertyChange(java.util.List<CarPropertyEvent> events) {
                        for (CarPropertyEvent event : events) {
                            onPropertyChange(event);
                        }
                    }

                    @Override
                    public void onPropertyChange(CarPropertyEvent event) {
                        if (fail[0]) {
                            throw new IllegalStateException("receiver unavailable");
                        }
                    }

                    @Override
                    public void onPropertySetError(int property, int area) {}
                });
        retry.subscribeProperty(SOC, 0);
        rejects(() -> retry.submit(BOOT, 7, 23, SHA));
        assert retry.status().generation == 7 && !retry.status().callbackReturned;
        assert retry.getProperty(SOC, 0).getValue().equals(Float.valueOf(23));
        fail[0] = false;
        assert retry.submit(BOOT, 7, 23, SHA).callbackReturned;
        rejects(() ->
                retry.submit(
                        BOOT,
                        7,
                        23,
                        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
        final PassionMcuProperty reentrant = new PassionMcuProperty(new Original(), BOOT);
        reentrant.setListener(
                new IProperty.Receiver() {
                    @Override
                    public void onPropertyChange(java.util.List<CarPropertyEvent> events) {
                        for (CarPropertyEvent e : events) {
                            onPropertyChange(e);
                        }
                    }

                    @Override
                    public void onPropertyChange(CarPropertyEvent event) {
                        if (reentrant.status().generation == 1) {
                            reentrant.submit(BOOT, 2, 62, SHA);
                        }
                    }

                    @Override
                    public void onPropertySetError(int property, int area) {}
                });
        reentrant.subscribeProperty(SOC, 0);
        assert !reentrant.submit(BOOT, 1, 25, SHA).callbackReturned;
        assert reentrant.status().generation == 2 && reentrant.status().soc == 62;
        assert reentrant.submit(BOOT, 2, 62, SHA).callbackReturned;
        System.out.println(
                "PASS MCU snapshot generations, actual receiver callback, failure retry and"
                        + " reentrant ownership");
    }
}
