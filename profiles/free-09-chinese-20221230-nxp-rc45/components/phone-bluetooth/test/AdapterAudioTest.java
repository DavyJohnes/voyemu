package com.qinggan.bluetooth.internal.ivt8;

import android.os.Handler;
import android.os.PeerTestRuntime;

import com.qinggan.bluetooth.profile.Hfp;
import com.qinggan.bluetooth.profile.IHfpCallback;

import java.lang.reflect.Proxy;
import java.util.concurrent.atomic.AtomicInteger;

public final class AdapterAudioTest {
    private static void need(boolean value, String reason) {
        if (!value) {
            throw new AssertionError(reason);
        }
    }

    public static void main(String[] args) throws Exception {
        System.setProperty("phone.peer.test.files", args[0]);
        PeerTestRuntime.enter("phone");
        Handler.immediate = true;
        Ivt8BTAdapter adapter = Ivt8BTAdapter.getInstance(new android.content.Context(), null);
        PeerTestRuntime.install(adapter);
        Hfp hfp = adapter.getHfp(null);
        hfp.connectAudio();
        need(!hfp.isAudioOn(), "disconnected route");
        adapter.control("connect", "{}");
        hfp.connectAudio();
        need(!hfp.isAudioOn(), "idle route");
        adapter.control("incoming", "{\"number\":\"+70000000001\"}");
        adapter.control("answer", "{}");
        AtomicInteger established = new AtomicInteger(), released = new AtomicInteger();
        IHfpCallback callback =
                (IHfpCallback)
                        Proxy.newProxyInstance(
                                AdapterAudioTest.class.getClassLoader(),
                                new Class[] {IHfpCallback.class},
                                (object, method, values) -> {
                                    if (method.getDeclaringClass() == Object.class) {
                                        if (method.getName().equals("equals")) {
                                            return object == values[0];
                                        }

                                        if (method.getName().equals("hashCode")) {
                                            return System.identityHashCode(object);
                                        }

                                        return "audio";
                                    }

                                    if (method.getName().equals("onAudioEstablished")) {
                                        need(
                                                "FixtureAndroidMain"
                                                        .equals(Thread.currentThread().getName()),
                                                "main callback thread");
                                        established.incrementAndGet();
                                        need(hfp.isAudioOn(), "state before callback");
                                        hfp.scoAudioSwitch(adapter.getConnectedDevice(), true);
                                    }

                                    if (method.getName().equals("onAudioReleased")) {
                                        released.incrementAndGet();
                                        need(!hfp.isAudioOn(), "released state before callback");
                                    }

                                    return null;
                                });
        IHfpCallback broken =
                (IHfpCallback)
                        Proxy.newProxyInstance(
                                AdapterAudioTest.class.getClassLoader(),
                                new Class[] {IHfpCallback.class},
                                (object, method, values) -> {
                                    if (method.getDeclaringClass() == Object.class) {
                                        if (method.getName().equals("equals")) {
                                            return object == values[0];
                                        }

                                        if (method.getName().equals("hashCode")) {
                                            return System.identityHashCode(object);
                                        }

                                        return "broken-audio";
                                    }

                                    if (method.getName().startsWith("onAudio")) {
                                        throw new UnsupportedOperationException(
                                                "faulty OEM consumer");
                                    }

                                    return null;
                                });
        hfp.registerListener(broken);
        hfp.registerListener(callback);
        hfp.connectAudio();
        need(hfp.isAudioOn() && established.get() == 1, "establish");
        hfp.connectAudio();
        need(established.get() == 1, "repeat reentrant establishment");
        hfp.disConnectAudio();
        need(!hfp.isAudioOn() && released.get() == 1, "release");
        hfp.connectAudio();
        adapter.control("hangup", "{}");
        need(!hfp.isAudioOn() && released.get() == 2, "hangup clears route");
        need(!hfp.scoAudioSwitch(adapter.getConnectedDevice(), true), "idle rejected");
        adapter.control("incoming", "{\"number\":\"+70000000001\"}");
        hfp.connectAudio();
        int beforeDisconnect = released.get();
        adapter.control("disconnect", "{}");
        need(
                !hfp.isAudioOn() && released.get() == beforeDisconnect + 1,
                "disconnect clears route exactly once");

        try {
            adapter.control("disconnect", "{}");
            throw new AssertionError("disconnected accepted");
        } catch (IllegalStateException expected) {
            need(
                    "PHONE_DISCONNECTED".equals(expected.getMessage()),
                    "wrong disconnected rejection");
        }

        need(released.get() == beforeDisconnect + 1, "rejected disconnect replayed callback");
        adapter.control("connect", "{}");
        adapter.control("incoming", "{\"number\":\"+70000000001\"}");
        hfp.connectAudio();
        adapter.setPowerOnHandler(false);
        need(!hfp.isAudioOn(), "OFF clears route");
        hfp.connectAudio();
        need(!hfp.isAudioOn(), "OFF cannot reconnect");
        System.out.println(
                "NXP AdapterAudioTest PASS idle/OFF rejection, established/released callbacks,"
                        + " reentrant idempotence, hangup/OFF reset");
    }
}
