package fixture;

import android.content.Context;
import android.os.Handler;
import android.os.PeerTestRuntime;

import com.qinggan.bluetooth.internal.ivt8.Ivt8BTAdapter;
import com.qinggan.bluetooth.profile.Hfp;
import com.qinggan.bluetooth.profile.IHfpCallback;

import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * Original OEM classification logic; a known local number is supplied only to the pure positive
 * control.
 */
public final class SubscriberNumberContractProbe {
    public static void run() throws Exception {
        PeerTestRuntime.enter("phone");
        final Ivt8BTAdapter adapter = Ivt8BTAdapter.getInstance(new Context(), null);
        final List<String> remoteCallbacks = new ArrayList<String>();
        IHfpCallback callback =
                (IHfpCallback)
                        Proxy.newProxyInstance(
                                IHfpCallback.class.getClassLoader(),
                                new Class<?>[] {IHfpCallback.class},
                                (proxy, method, args) -> {
                                    if ("equals".equals(method.getName())) {
                                        return proxy == args[0];
                                    }

                                    if ("hashCode".equals(method.getName())) {
                                        return System.identityHashCode(proxy);
                                    }

                                    if ("on3WayCallStateChanged".equals(method.getName())) {
                                        remoteCallbacks.add((String) args[2]);
                                    }

                                    return null;
                                });
        Hfp hfp = adapter.getHfp(callback);
        Handler.drain();
        adapter.control("connect", "{}");
        Handler.drain();
        hfp.dial("70000000001");
        Handler.drain();
        String remote = (String) ((Map) hfp.getCurrentCalls().get(0)).get("call_number");

        if (!"+70000000001".equals(remote)
                || !remote.equals(remoteCallbacks.get(remoteCallbacks.size() - 1))) {
            throw new AssertionError("remote +7 map/callback changed");
        }

        if (!remote.equals(adapter.phoneInfoSnapshot().getString("number"))) {
            throw new AssertionError("remote PhoneInfo +7 changed");
        }

        String snapshot = adapter.control("snapshot", "{}");

        if (!snapshot.contains("\"outgoing\":[\"+70000000001\"]")) {
            throw new AssertionError("remote outgoing journal changed");
        }

        boolean noarg = isWeChat(amend(remote), hfp.getPhoneNumber()),
                address = isWeChat(amend(remote), hfp.getPhoneNumber("02:00:00:00:00:01"));
        verifyKnownSubscriberControls();
        System.out.println(
                "isWeChat_noarg="
                        + noarg
                        + " isWeChat_address="
                        + address
                        + " remote_map_callback_phoneinfo_journal_plus7=true"
                        + " known_self_positive_control=true");

        if (noarg || address) {
            throw new AssertionError(
                    "remote caller incorrectly exposed as local subscriber, causing original"
                            + " isWeChat self-call classification");
        }

        verifyUnknownSubscriber(hfp);
        hfp.hangup();
        Handler.drain();
        verifyIncoming(adapter, hfp, remoteCallbacks);
    }

    private static void verifyKnownSubscriberControls() {
        if (!isWeChat("70000000001", "+70000000001")
                || isWeChat("70000000001", "+70000000002")
                || !isWeChat("10000000", "")
                || !isWeChat("0000", "")
                || !isWeChat("", null)) {
            throw new AssertionError("original pure known-self/special-number controls");
        }
    }

    private static void verifyIncoming(Ivt8BTAdapter adapter, Hfp hfp, List<String> remoteCallbacks)
            throws Exception {
        adapter.control("incoming", "{\"number\":\"+70000000002\"}");
        Handler.drain();
        String remote = (String) ((Map) hfp.getCurrentCalls().get(0)).get("call_number");

        if (!"+70000000002".equals(remote)
                || !remote.equals(remoteCallbacks.get(remoteCallbacks.size() - 1))
                || !remote.equals(adapter.phoneInfoSnapshot().getString("number"))
                || isWeChat(amend(remote), hfp.getPhoneNumber())
                || isWeChat(amend(remote), hfp.getPhoneNumber("02:00:00:00:00:01"))) {
            throw new AssertionError("incoming remote +7 or unknown local subscriber changed");
        }

        if (!adapter.control("snapshot", "{}").contains(":incoming")) {
            throw new AssertionError("incoming remote journal missing");
        }

        hfp.hangup();
        Handler.drain();
    }

    private static void verifyUnknownSubscriber(Hfp hfp) {
        if (!"".equals(hfp.getPhoneNumber())
                || !"".equals(hfp.getPhoneNumber("02:00:00:00:00:01"))
                || !"".equals(hfp.getPhoneNumber("02:00:00:00:00:02"))) {
            throw new AssertionError("unknown local subscriber contract");
        }
    }

    private static boolean isWeChat(String number, String subscriber) {
        return number == null
                || number.isEmpty()
                || "10000000".equals(number)
                || number.matches("^0{4,}")
                || amend(subscriber).equals(number);
    }

    private static String amend(String value) {
        if (value == null || value.isEmpty()) {
            return "";
        }

        value = value.trim();

        if ("+".equals(value)) {
            return "+";
        }

        if (value.startsWith("+")) {
            value = value.substring(1);
        }

        if (value.length() > 2 && value.startsWith("86")) {
            return value.substring(2);
        }

        return value.replaceAll("-", "").replaceAll(" ", "");
    }
}
