package com.qinggan.bluetooth.internal.ivt8;

import android.os.Binder;
import android.os.Bundle;
import android.os.Handler;
import android.os.Parcel;
import android.os.PeerTestRuntime;

import com.qinggan.bluetooth.profile.IBTCallback;
import com.qinggan.bluetooth.profile.IPbapCallback;

import java.lang.reflect.Proxy;
import java.util.concurrent.atomic.AtomicInteger;

public final class AdapterPowerDeliveryTest {
    private static IPbapCallback pbapCallback(AtomicInteger syncEvents, AtomicInteger contacts) {
        return (IPbapCallback)
                Proxy.newProxyInstance(
                        AdapterPowerDeliveryTest.class.getClassLoader(),
                        new Class[] {IPbapCallback.class},
                        (object, method, values) -> {
                            if (method.getDeclaringClass() == Object.class) {
                                if (method.getName().equals("equals")) {
                                    return object == values[0];
                                }

                                if (method.getName().equals("hashCode")) {
                                    return System.identityHashCode(object);
                                }

                                return "pbap";
                            }

                            if (method.getName().equals("onSyncStateChanged")) {
                                for (Object state : values) {
                                    if (!java.util.Arrays.asList(96, 97, 98)
                                            .contains((Integer) state)) {
                                        throw new AssertionError("private PBAP state leaked");
                                    }
                                }

                                syncEvents.incrementAndGet();
                            }

                            if (method.getName().equals("onContactItemFetched")) {
                                contacts.incrementAndGet();
                            }

                            return null;
                        });
    }

    private static IBTCallback brokenListener() {
        return (IBTCallback)
                Proxy.newProxyInstance(
                        AdapterPowerDeliveryTest.class.getClassLoader(),
                        new Class[] {IBTCallback.class},
                        (object, method, values) -> {
                            if (method.getDeclaringClass() == Object.class) {
                                if (method.getName().equals("equals")) {
                                    return object == values[0];
                                }

                                if (method.getName().equals("hashCode")) {
                                    return System.identityHashCode(object);
                                }

                                return "broken";
                            }

                            throw new UnsupportedOperationException("consumer");
                        });
    }

    private static IBTCallback goodListener(AtomicInteger healthy) {
        return (IBTCallback)
                Proxy.newProxyInstance(
                        AdapterPowerDeliveryTest.class.getClassLoader(),
                        new Class[] {IBTCallback.class},
                        (object, method, values) -> {
                            if (method.getDeclaringClass() == Object.class) {
                                if (method.getName().equals("equals")) {
                                    return object == values[0];
                                }

                                if (method.getName().equals("hashCode")) {
                                    return System.identityHashCode(object);
                                }

                                return "good";
                            }

                            if (method.getName().equals("onConnectionStateChanged")
                                    || method.getName().equals("onBTStateChanged")) {
                                healthy.incrementAndGet();
                            }

                            return null;
                        });
    }

    private static void requireCommittedOff(
            Ivt8BTAdapter adapter, int healthy, String snapshot, java.util.List<String> received) {
        if (adapter.isEnabled()
                || adapter.getState() != 0
                || healthy != 2
                || snapshot == null
                || !snapshot.contains("\"powered\":false")
                || !snapshot.contains("\"connected\":false")) {
            throw new AssertionError("committed OFF delivery");
        }

        for (String value : received) {
            if (value.contains("\"powered\":false")
                    && (!value.contains("\"pbap_connected\":false")
                            || !value.contains("\"connected\":false"))) {
                throw new AssertionError("incoherent OFF snapshot");
            }
        }
    }

    public static void main(String[] args) throws Exception {
        System.setProperty("phone.peer.test.files", args[0]);
        PeerTestRuntime.enter("phone");
        Handler.immediate = true;
        Ivt8BTAdapter adapter = Ivt8BTAdapter.getInstance(new android.content.Context(), null);
        PeerTestRuntime.install(adapter);
        adapter.control("connect", "{}");
        AtomicInteger syncEvents = new AtomicInteger(), contacts = new AtomicInteger();
        IPbapCallback pbapCallback = pbapCallback(syncEvents, contacts);

        if (!adapter.getPbap(pbapCallback).startContactsSync()) {
            throw new AssertionError("sync not started");
        }

        if (syncEvents.get() != 2 || contacts.get() != 2) {
            throw new AssertionError("sync delivery");
        }

        adapter.control("incoming", "{\"number\":\"+70000000001\"}");
        AtomicInteger healthy = new AtomicInteger(), deliveries = new AtomicInteger();
        String[] snapshot = {null};
        java.util.List<String> received = new java.util.concurrent.CopyOnWriteArrayList<>();
        Binder callback =
                new Binder() {
                    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
                        data.enforceInterface("com.voyemu.phone.peer.callback.v1");
                        snapshot[0] = data.readString();
                        received.add(snapshot[0]);
                        deliveries.incrementAndGet();

                        return true;
                    }
                };
        Bundle initial = adapter.subscribePeer(callback, 1000, 99);
        PeerTestRuntime.enter("launcher");
        java.lang.reflect.Constructor<Ivt8BTAdapter> constructor =
                Ivt8BTAdapter.class.getDeclaredConstructor(
                        android.content.Context.class, com.qinggan.bluetooth.BTDeviceManager.class);
        constructor.setAccessible(true);
        Ivt8BTAdapter mirror = constructor.newInstance(new android.content.Context(), null);
        java.lang.reflect.Method accept =
                Ivt8BTAdapter.class.getDeclaredMethod("acceptRemote", org.json.JSONObject.class);
        accept.setAccessible(true);
        accept.invoke(mirror, new org.json.JSONObject(initial.getString("snapshot")));

        if (!mirror.isEnabled() || !mirror.getPbap(null).isConnected()) {
            throw new AssertionError("initial mirror prerequisite");
        }

        PeerTestRuntime.enter("phone");

        IBTCallback broken = brokenListener();
        IBTCallback good = goodListener(healthy);
        adapter.registerListener(broken);
        adapter.registerListener(good);
        adapter.setPowerOnHandler(false);
        long deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(2);

        while ((snapshot[0] == null || !snapshot[0].contains("\"powered\":false"))
                && System.nanoTime() < deadline) {
            Thread.sleep(2);
        }

        requireCommittedOff(adapter, healthy.get(), snapshot[0], received);

        for (String value : received) {
            accept.invoke(mirror, new org.json.JSONObject(value));
        }

        if (mirror.isEnabled()
                || mirror.getPbap(null).isConnected()
                || mirror.getHfp(null).isConnected()) {
            throw new AssertionError("mirror rejected coherent OFF");
        }

        int before = healthy.get();
        adapter.setPowerOnHandler(false);

        if (healthy.get() != before) {
            throw new AssertionError("replayed transition");
        }

        if (!adapter.control("snapshot", "{}").contains("\"state\":131")) {
            throw new AssertionError("call still ringing");
        }

        System.out.println(
                "NXP AdapterPowerDeliveryTest PASS actual adapter: first consumer throws, later"
                    + " connection+power callbacks run, OFF publication received, call terminated,"
                    + " repeat idempotent");
    }
}
