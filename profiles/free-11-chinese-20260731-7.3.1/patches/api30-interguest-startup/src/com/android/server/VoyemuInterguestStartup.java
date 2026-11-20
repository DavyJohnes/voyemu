package com.android.server;

import android.util.Log;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.InetAddress;

/** Provisions the isolated Android-to-QNX Ethernet link once per guest boot. */
final class VoyemuInterguestStartup {
    private static final String TAG = "VoyemuInterguestStartup";
    private static final String MAC = "52:54:00:76:6f:21";
    private static final String ADDRESS = "172.16.100.21/24";
    private static final String SUBNET = "172.16.100.0/24";
    private static final int NET_ID = 65000;
    private static final int ATTEMPTS = 5;
    private static boolean scheduled;

    private VoyemuInterguestStartup() {}

    static synchronized void schedule(NetworkManagementService manager) {
        if (scheduled || manager == null) {
            return;
        }

        try {
            new Thread(new Attempt(manager), TAG).start();
            scheduled = true;
        } catch (RuntimeException failure) {
            Log.w(TAG, "Unable to schedule Ethernet setup", failure);
        }
    }

    private static final class Attempt implements Runnable {
        private final NetworkManagementService manager;

        Attempt(NetworkManagementService manager) {
            this.manager = manager;
        }

        @Override
        public void run() {
            for (int attempt = 1; attempt <= ATTEMPTS; ++attempt) {
                try {
                    configure(manager);
                    return;
                } catch (ReflectiveOperationException | RuntimeException failure) {
                    if (attempt == ATTEMPTS) {
                        Log.w(TAG, "Isolated Ethernet setup failed", failure);
                        return;
                    }
                }

                try {
                    Thread.sleep(500);
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    return;
                }
            }
        }
    }

    private static void configure(NetworkManagementService manager)
            throws ReflectiveOperationException {
        Class<?> service = manager.getClass();
        Class<?> config = Class.forName("android.net.InterfaceConfiguration");
        Class<?> link = Class.forName("android.net.LinkAddress");
        Class<?> route = Class.forName("android.net.RouteInfo");
        Method getConfig = service.getMethod("getInterfaceConfig", String.class);
        Method getMac = config.getMethod("getHardwareAddress");
        String selected = null;
        String[] names = (String[]) service.getMethod("listInterfaces").invoke(manager);

        if (names == null || names.length > 128) {
            throw new IllegalStateException("Invalid Ethernet interface inventory");
        }

        for (String name : names) {
            if (name == null || !name.matches("[A-Za-z0-9_.:-]{1,15}")) {
                throw new IllegalStateException("Invalid Ethernet interface name");
            }
            Object candidate = getConfig.invoke(manager, name);
            if (candidate != null && MAC.equalsIgnoreCase((String) getMac.invoke(candidate))) {
                if (selected != null) {
                    throw new IllegalStateException("Ambiguous isolated Ethernet interface");
                }
                selected = name;
            }
        }
        if (selected == null) {
            throw new IllegalStateException("Isolated Ethernet interface absent");
        }

        Object current = getConfig.invoke(manager, selected);
        if (current == null || !MAC.equalsIgnoreCase((String) getMac.invoke(current))) {
            throw new IllegalStateException("Isolated Ethernet interface changed");
        }
        config.getMethod("setLinkAddress", link)
                .invoke(current, link.getConstructor(String.class).newInstance(ADDRESS));
        config.getMethod("setInterfaceUp").invoke(current);
        service.getMethod("setInterfaceConfig", String.class, config)
                .invoke(manager, selected, current);

        Class<?> binder = Class.forName("android.os.IBinder");
        Object endpoint = Class.forName("android.os.ServiceManager")
                .getMethod("getService", String.class).invoke(null, "netd");
        if (endpoint == null) {
            throw new IllegalStateException("netd unavailable");
        }
        Class<?> netd = Class.forName("android.net.INetd");
        Object daemon = Class.forName("android.net.INetd$Stub")
                .getMethod("asInterface", binder).invoke(null, endpoint);
        if (!Boolean.TRUE.equals(netd.getMethod("isAlive").invoke(daemon))) {
            throw new IllegalStateException("netd unavailable");
        }

        fixedRequest(daemon, netd.getMethod("networkCreatePhysical", int.class, int.class),
                NET_ID, netd.getField("PERMISSION_NONE").getInt(null));
        fixedRequest(manager, service.getMethod("addInterfaceToNetwork", String.class, int.class),
                selected, NET_ID);

        Object connected = route.getConstructor(link, InetAddress.class, String.class)
                .newInstance(link.getConstructor(String.class).newInstance(SUBNET), null, selected);
        Method legacyRoute = service.getMethod("addLegacyRouteForNetId", int.class, route, int.class);
        fixedRequest(manager, legacyRoute, NET_ID, connected, 0);
        fixedRequest(manager, legacyRoute, NET_ID, connected, 10000);
    }

    private static void fixedRequest(Object receiver, Method method, Object... arguments)
            throws ReflectiveOperationException {
        try {
            method.invoke(receiver, arguments);
        } catch (InvocationTargetException wrapped) {
            Throwable failure = wrapped.getCause();
            if (failure == null
                    || !failure.getClass().getName().equals("android.os.ServiceSpecificException")
                    || failure.getClass().getField("errorCode").getInt(failure) != 17) {
                throw wrapped;
            }
        }
    }
}
