package com.voyemu.oem;

import android.net.ConnectivityManager;
import android.net.NetworkInfo;

/** Keep the OEM transport predicates when an AOSP guest lacks an interface. */
public final class NetworkConnectivity {
    private NetworkConnectivity() {}

    public static boolean isConnected(ConnectivityManager manager, boolean includeMobile) {
        if (manager == null) {
            return false;
        }

        if (connected(manager.getNetworkInfo(ConnectivityManager.TYPE_WIFI))) {
            return true;
        }

        if (includeMobile && connected(manager.getNetworkInfo(ConnectivityManager.TYPE_MOBILE))) {
            return true;
        }

        return connected(manager.getNetworkInfo(ConnectivityManager.TYPE_ETHERNET));
    }

    private static boolean connected(NetworkInfo network) {
        return network != null && network.isConnected();
    }
}
