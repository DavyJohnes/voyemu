package fixture;

import android.content.Context;
import android.os.Handler;
import android.os.PeerTestRuntime;

import com.qinggan.bluetooth.internal.ivt8.Ivt8BTAdapter;
import com.qinggan.bluetooth.profile.A2dp;
import com.qinggan.bluetooth.profile.BTAdapter;
import com.qinggan.bluetooth.profile.BTDevice;
import com.qinggan.bluetooth.profile.IA2dpCallback;
import com.qinggan.bluetooth.profile.IBTCallback;
import com.qinggan.bluetooth.profile.SimpleHfpCallback;

import org.json.JSONObject;

import java.lang.reflect.Method;
import java.lang.reflect.Proxy;

/** Original Launcher A2dpManager-shaped call chain, with no null guard added by this test. */
public final class A2dpOriginalConsumerProbe {
    private final Ivt8BTAdapter adapter;
    private final A2dp a2dp;
    private int healthy, completed, state = -1;
    private Throwable failure;

    private A2dpOriginalConsumerProbe() {
        PeerTestRuntime.enter("launcher");
        adapter = Ivt8BTAdapter.getInstance(new Context(), null);
        a2dp = adapter.getA2dp(null);

        synchronized (PeerTestRuntime.MAIN_LOCK) {
            adapter.registerListener(listener(false));
            adapter.registerListener(listener(true));
            adapter.getHfp(new HfpReady());
        }
    }

    public static A2dpOriginalConsumerProbe create() {
        return new A2dpOriginalConsumerProbe();
    }

    private IBTCallback listener(final boolean healthyListener) {
        return (IBTCallback)
                Proxy.newProxyInstance(
                        IBTCallback.class.getClassLoader(),
                        new Class<?>[] {IBTCallback.class},
                        (proxy, method, args) -> {
                            if ("equals".equals(method.getName())) {
                                return proxy == args[0];
                            }

                            if ("hashCode".equals(method.getName())) {
                                return System.identityHashCode(proxy);
                            }

                            if ("onConnectionStateChanged".equals(method.getName())
                                    && ((Integer) args[2]) == 16) {
                                if (healthyListener) {
                                    healthy++;
                                } else {
                                    try {
                                        notifyConnectionState((BTDevice) args[0]);
                                        completed++;
                                    } catch (RuntimeException broken) {
                                        failure = broken;
                                        throw broken;
                                    }
                                }
                            }

                            return null;
                        });
    }

    private void notifyConnectionState(BTDevice device) {
        refreshState(false, device);
    }

    private void refreshState(boolean notify, BTDevice device) {
        initConnectedDevice(device);
    }

    private void initConnectedDevice(BTDevice device) {
        state = a2dp.getConnectionState(device);
    }

    public String inspect() throws Exception {
        Handler.drain();
        Method snapshot = adapter.getClass().getDeclaredMethod("snapshot");
        snapshot.setAccessible(true);

        return new JSONObject(snapshot.invoke(adapter).toString())
                .put("test_completed", completed)
                .put("test_healthy", healthy)
                .put("test_a2dp_state", state)
                .put("test_failure", failure == null ? "none" : failure.getClass().getName())
                .toString();
    }

    public void printFailure() {
        if (failure != null) {
            failure.printStackTrace(System.err);
        }
    }

    public void inactiveContract() throws Exception {
        if (a2dp == null || adapter.getA2dp(null) != a2dp) {
            throw new AssertionError("stable nonnull inactive A2dp");
        }

        BTDevice device = adapter.getConnectedDevice();

        if (a2dp.getConnectionState() != 19
                || a2dp.getConnectionState(device) != 19
                || a2dp.isConnected()) {
            throw new AssertionError("A2dp must stay disconnected while HFP connects");
        }

        if (a2dp.isProfileReady()) {
            throw new AssertionError("inactive A2dp is not ready");
        }

        try {
            a2dp.getConnectionState((BTDevice) null);
            throw new AssertionError("null device accepted");
        } catch (IllegalArgumentException expected) {
        }

        final int[] callbacks = {0};
        IA2dpCallback listener =
                (IA2dpCallback)
                        Proxy.newProxyInstance(
                                IA2dpCallback.class.getClassLoader(),
                                new Class<?>[] {IA2dpCallback.class},
                                (proxy, method, args) -> {
                                    if ("equals".equals(method.getName())) {
                                        return proxy == args[0];
                                    }

                                    if ("hashCode".equals(method.getName())) {
                                        return System.identityHashCode(proxy);
                                    }

                                    callbacks[0]++;

                                    return null;
                                });
        verifyInactiveMedia(listener, callbacks);
        verifyReadyLifecycle(listener, callbacks);
        String before = inspect();
        verifyRejectedMutations(device);
    }

    private void verifyInactiveMedia(IA2dpCallback listener, int[] callbacks) throws Exception {
        if (adapter.getA2dp(listener) != a2dp) {
            throw new AssertionError("callback accessor replaces A2dp");
        }

        A2dp.class.getMethod("registerListener", IA2dpCallback.class).invoke(a2dp, listener);

        for (String method :
                new String[] {
                    "isPlaying",
                    "play",
                    "pause",
                    "stop",
                    "backward",
                    "forward",
                    "startFastForward",
                    "startRewind",
                    "stopFastForward",
                    "stopRewind",
                    "volumnDown",
                    "volumnUp"
                }) {
            if (!Boolean.FALSE.equals(A2dp.class.getMethod(method).invoke(a2dp))) {
                throw new AssertionError("inactive media control " + method);
            }
        }

        Object devices = A2dp.class.getMethod("getConnectedDevices").invoke(a2dp);

        if (!(devices instanceof java.util.List) || !((java.util.List) devices).isEmpty()) {
            throw new AssertionError("inactive A2dp connected devices");
        }

        for (String method :
                new String[] {"initListener", "mediaBrowsing", "nowPlayingReq", "onDestory"}) {
            A2dp.class.getMethod(method).invoke(a2dp);
        }

        A2dp.class.getMethod("playMediaItem", byte[].class).invoke(a2dp, new byte[] {1, 2});
        A2dp.class
                .getMethod("changeFolderPath", int.class, byte[].class)
                .invoke(a2dp, 1, new byte[] {1, 2});
        A2dp.class.getMethod("unregisterListener", IA2dpCallback.class).invoke(a2dp, listener);
        Handler.drain();

        if (callbacks[0] != 0) {
            throw new AssertionError("inactive A2dp emitted readiness/audio/media callback");
        }
    }

    private void verifyReadyLifecycle(IA2dpCallback listener, int[] callbacks) throws Exception {
        A2dp ready =
                (A2dp)
                        Class.forName(
                                        "com.qinggan.bluetooth.profile.A2dpReadyFixture",
                                        true,
                                        A2dp.class.getClassLoader())
                                .getConstructor(Context.class)
                                .newInstance(new Context());

        ready.registerListener(listener);
        ready.registerListener(listener);
        Handler.drain();

        if (callbacks[0] != 1) {
            throw new AssertionError("positive OEM base readiness/dedup control");
        }

        ready.unregisterListener(listener);
        ready.registerListener(listener);
        Handler.drain();

        if (callbacks[0] != 2) {
            throw new AssertionError("positive OEM base unregister control");
        }

        java.lang.reflect.Field bound = BTAdapter.class.getDeclaredField("mA2dp");
        bound.setAccessible(true);

        if (bound.get(adapter) != a2dp) {
            throw new AssertionError("original inherited A2dp lifecycle field not bound");
        }

        int destroyed = A2dp.class.getField("testDestroyed").getInt(a2dp);
        adapter.onDestory();

        if (A2dp.class.getField("testDestroyed").getInt(a2dp) != destroyed + 1) {
            throw new AssertionError("original A2dp lifecycle branch skipped");
        }
    }

    private void verifyRejectedMutations(BTDevice device) throws Exception {
        if (a2dp.connect(device)
                || a2dp.connect("02:00:00:00:00:01")
                || a2dp.disconnect(device)
                || a2dp.disconnect("02:00:00:00:00:01")) {
            throw new AssertionError("inactive A2dp mutator accepted");
        }

        Handler.drain();

        if (a2dp.isConnected()
                || a2dp.getConnectionState() != 19
                || !adapter.getHfp(null).isConnected()) {
            throw new AssertionError("inactive A2dp mutates profile state");
        }
    }

    private static final class HfpReady extends SimpleHfpCallback {
        public void onReady() {}

        public void onStateChanged(BTDevice d, int before, int after) {}

        public void on3WayCallStateChanged(int s, int id, String n) {}

        public void onCallStateChanged(int s, String n) {}

        public void onPhoneMessage(com.qinggan.bluetooth.bean.BTPhoneMessage m) {}

        public void onAudioEstablished() {}

        public void onAudioReleased() {}

        public void onRingFromPhone(boolean b) {}
    }
}
