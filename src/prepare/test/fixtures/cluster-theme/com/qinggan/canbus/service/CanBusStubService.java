package com.qinggan.canbus.service;

import com.qinggan.canbus.VehicleState;

final class CanBusStubService {
    final ClusterTheme theme = new ClusterTheme(new android.content.Context());
    ClusterThemeBinder binder;
    IpkInfoSync ipk =
            new IpkInfoSync(
                    new IpkLink(
                            new java.net.InetSocketAddress(
                                    java.net.InetAddress.getLoopbackAddress(), 9)));

    volatile java.util.concurrent.CountDownLatch applied;

    IpkInfoSync ipk() {
        return ipk;
    }

    int[] clusterThemeSnapshot() {
        return theme.snapshot();
    }

    int getVehicleState(VehicleState state) {
        return state == VehicleState.IPK_THEME_SKIN ? theme.skin() : theme.mode();
    }

    void setVehicleState(VehicleState state, int value) {
        theme.write(state, value, new java.util.HashMap<>());

        if (applied != null) {
            applied.countDown();
        }

        binder.changed();
    }
}
