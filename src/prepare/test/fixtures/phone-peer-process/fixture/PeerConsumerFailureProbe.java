package fixture;

import android.content.Context;
import android.os.Handler;
import android.os.PeerTestRuntime;

import com.qinggan.bluetooth.internal.ivt8.Ivt8BTAdapter;
import com.qinggan.bluetooth.profile.BTDevice;
import com.qinggan.bluetooth.profile.Hfp;
import com.qinggan.bluetooth.profile.IBTCallback;
import com.qinggan.bluetooth.profile.IHfpCallback;
import com.qinggan.bluetooth.profile.SimpleHfpCallback;

import org.json.JSONObject;

import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.Map;

/**
 * Genuine OEM listener interfaces; deliberately faulty consumers must not invalidate valid mirrored
 * state.
 */
public final class PeerConsumerFailureProbe {
    private final Ivt8BTAdapter adapter;
    private final String stage;
    private Hfp hfp;
    private int failed, healthyConnections, healthyIbtConnections, healthyCalls;
    private boolean committedBeforeThrow;
    private final java.util.List<IHfpCallback> listeners = new java.util.ArrayList<IHfpCallback>();

    private PeerConsumerFailureProbe(String stage) {
        this.stage = stage;
        PeerTestRuntime.enter("launcher");
        adapter = Ivt8BTAdapter.getInstance(new Context(), null);

        synchronized (PeerTestRuntime.MAIN_LOCK) {
            if ("ibtconnection".equals(stage)) {
                adapter.registerListener(ibt(true));
                adapter.registerListener(ibt(false));
            }

            if (!"ibtconnection".equals(stage)) {
                hfp = adapter.getHfp(hfp(true));
            }

            hfp = adapter.getHfp(hfp(false));
        }
    }

    public static PeerConsumerFailureProbe create(String stage) {
        return new PeerConsumerFailureProbe(stage);
    }

    private IBTCallback ibt(final boolean broken) {
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
                                assertCommitted(131);

                                if (broken && failed++ == 0) {
                                    throw new UnsupportedOperationException("fixture consumer");
                                }

                                if (!broken) {
                                    healthyIbtConnections++;
                                }
                            }

                            return null;
                        });
    }

    private IHfpCallback hfp(final boolean broken) {
        SimpleHfpCallback callback =
                new SimpleHfpCallback() {
                    public void onReady() {}

                    public void onStateChanged(BTDevice device, int before, int state) {
                        if (state == 16) {
                            assertCommitted(131);

                            if (broken && "hfpconnection".equals(stage) && failed++ == 0) {
                                throw new UnsupportedOperationException("fixture consumer");
                            }

                            if (!broken) {
                                healthyConnections++;
                            }
                        }
                    }

                    public void on3WayCallStateChanged(int state, int id, String number) {
                        if (state == 130) {
                            assertCommitted(130);

                            if (getCurrentDevice() != adapter.getConnectedDevice()) {
                                throw new AssertionError(
                                        "OEM currentDevice binding before callout");
                            }

                            if (broken && "hfpcall".equals(stage) && failed++ == 0) {
                                throw new UnsupportedOperationException("fixture consumer");
                            }

                            if (!broken) {
                                healthyCalls++;
                            }
                        }
                    }

                    public void onCallStateChanged(int state, String number) {}

                    public void onPhoneMessage(com.qinggan.bluetooth.bean.BTPhoneMessage message) {}

                    public void onAudioEstablished() {}

                    public void onAudioReleased() {}

                    public void onRingFromPhone(boolean ring) {}
                };

        listeners.add(callback);

        return callback;
    }

    private void assertCommitted(int callState) {
        if (!adapter.getHfp(null).isConnected()
                || adapter.getHfp(null).getCallState() != callState
                || android.os.Process.myPid() != 4102) {
            throw new AssertionError("callback before main/getter commit");
        }

        if (callState == 130) {
            Map call = (Map) adapter.getHfp(null).getCurrentCalls().get(0);

            if (!"+70000000001".equals(call.get("call_number"))) {
                throw new AssertionError("callback number getter");
            }
        }

        committedBeforeThrow = true;
    }

    public String inspect() throws Exception {
        Handler.drain();
        Method method = adapter.getClass().getDeclaredMethod("snapshot");
        method.setAccessible(true);
        JSONObject result = new JSONObject(method.invoke(adapter).toString());

        return result.put("test_failed", failed)
                .put("test_committed", committedBeforeThrow)
                .put("test_healthy_connections", healthyConnections)
                .put("test_healthy_ibt_connections", healthyIbtConnections)
                .put("test_healthy_calls", healthyCalls)
                .toString();
    }

    public void unregister() {
        PeerTestRuntime.enter("launcher");

        for (IHfpCallback callback : listeners) {
            hfp.unregisterListener(callback);
        }

        Handler.drain();
    }
}
