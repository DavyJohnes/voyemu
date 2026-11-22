package com.voyemu.api;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.InetAddress;
import java.util.Collections;
import java.util.List;

/**
 * Fixed Ethernet setup with the multimedia address of the profile's interguest contract; Binder
 * acknowledgements request routes, they do not prove reachability.
 */
public final class NetworkSetup {
    static final String MAC = "52:54:00:76:6f:21";
    static final int NETWORK_ID = 65000;

    interface Manager {
        String[] interfaces() throws Exception;

        String hardware(String iface) throws Exception;

        void configure(String iface) throws Exception;

        boolean configured(String iface) throws Exception;

        void requestRoutes(String iface) throws Exception;
    }

    static final class Result {
        final String iface, address;
        final List<Integer> networks;

        Result(String iface, String address, List<Integer> networks) {
            this.iface = iface;
            this.address = address;
            this.networks = networks;
        }

        String json() {
            return "{\"schema\":1,\"ok\":true,\"interface\":\""
                    + iface
                    + "\",\"address\":\""
                    + address
                    + "\",\"prefix\":24,\"local_route_requested\":true,\"network_ids\":"
                    + networks
                    + "}";
        }
    }

    /** A private IPv4 address of a /24 link, as the interguest contract names it. */
    static boolean address(String value) {
        return value.matches(
                        "(10|172\\.(1[6-9]|2[0-9]|3[01])|192\\.168)(\\.(25[0-5]|2[0-4][0-9]|1?[0-9]?[0-9])){2,3}")
                && value.chars().filter(c -> c == '.').count() == 3;
    }

    static Result apply(Manager manager, String address) throws Exception {
        String selected = null;
        String[] names = manager.interfaces();

        if (names == null || names.length > 128) {
            throw new IllegalStateException("interface_inventory");
        }

        for (String name : names) {
            if (name == null || !name.matches("[A-Za-z0-9_.:-]{1,15}")) {
                throw new IllegalStateException("interface_name");
            }

            if (MAC.equalsIgnoreCase(manager.hardware(name))) {
                if (selected != null) {
                    throw new IllegalStateException("interface_ambiguous");
                }

                selected = name;
            }
        }

        if (selected == null) {
            throw new IllegalStateException("interface_missing");
        }

        manager.configure(selected);

        if (!manager.configured(selected)) {
            throw new IllegalStateException("interface_readback");
        }

        manager.requestRoutes(selected);

        if (!manager.configured(selected)) {
            throw new IllegalStateException("interface_changed");
        }

        return new Result(selected, address, Collections.singletonList(NETWORK_ID));
    }

    static Method legacyCreate(Class<?> type) {
        try {
            return type.getMethod("createPhysicalNetwork", int.class, String.class);
        } catch (NoSuchMethodException absent) {
            return null;
        }
    }

    private static final class BinderManager implements Manager {
        final Class<?> api, binder, link, config, route;
        final Object manager;
        final String address, subnet;

        BinderManager(String address) throws Exception {
            this.address = address;
            subnet = address.substring(0, address.lastIndexOf('.')) + ".0/24";
            binder = Class.forName("android.os.IBinder");
            api = Class.forName("android.os.INetworkManagementService");
            manager = service("network_management", "android.os.INetworkManagementService");
            link = Class.forName("android.net.LinkAddress");
            config = Class.forName("android.net.InterfaceConfiguration");
            route = Class.forName("android.net.RouteInfo");
        }

        Object service(String name, String type) throws Exception {
            Object endpoint =
                    Class.forName("android.os.ServiceManager")
                            .getMethod("getService", String.class)
                            .invoke(null, name);
            long deadline = System.nanoTime() + 5_000_000_000L;

            while (endpoint == null) {
                long remaining = deadline - System.nanoTime();

                if (remaining <= 0) {
                    throw new IllegalStateException("service_unavailable");
                }

                Thread.sleep(Math.min(250L, Math.max(1L, remaining / 1_000_000L)));
                endpoint =
                        Class.forName("android.os.ServiceManager")
                                .getMethod("getService", String.class)
                                .invoke(null, name);
            }

            return Class.forName(type + "$Stub")
                    .getMethod("asInterface", binder)
                    .invoke(null, endpoint);
        }

        Object configuration(String iface) throws Exception {
            return api.getMethod("getInterfaceConfig", String.class).invoke(manager, iface);
        }

        Object connected(String iface) throws Exception {
            return route.getConstructor(link, InetAddress.class, String.class)
                    .newInstance(
                            link.getConstructor(String.class).newInstance(subnet), null, iface);
        }

        @Override
        public String[] interfaces() throws Exception {
            return (String[]) api.getMethod("listInterfaces").invoke(manager);
        }

        @Override
        public String hardware(String iface) throws Exception {
            return (String) config.getMethod("getHardwareAddress").invoke(configuration(iface));
        }

        @Override
        public void configure(String iface) throws Exception {
            if (configured(iface)) {
                return;
            }

            Object value = configuration(iface);
            config.getMethod("setLinkAddress", link)
                    .invoke(value, link.getConstructor(String.class).newInstance(address + "/24"));
            config.getMethod("setInterfaceUp").invoke(value);
            api.getMethod("setInterfaceConfig", String.class, config).invoke(manager, iface, value);
        }

        @Override
        public boolean configured(String iface) throws Exception {
            Object value = configuration(iface);
            Object address = config.getMethod("getLinkAddress").invoke(value);

            return MAC.equalsIgnoreCase(
                            (String) config.getMethod("getHardwareAddress").invoke(value))
                    && Boolean.TRUE.equals(config.getMethod("isUp").invoke(value))
                    && address != null
                    && address.equals(
                            ((InetAddress) link.getMethod("getAddress").invoke(address))
                                    .getHostAddress())
                    && Integer.valueOf(24)
                            .equals(link.getMethod("getPrefixLength").invoke(address));
        }

        private void idempotent(Object target, Method method, Object... arguments)
                throws Exception {
            try {
                method.invoke(target, arguments);
            } catch (InvocationTargetException wrapped) {
                Throwable failure = wrapped.getCause();
                boolean exists = false;

                if (failure != null
                        && failure.getClass()
                                .getName()
                                .equals("android.os.ServiceSpecificException")) {
                    exists = failure.getClass().getField("errorCode").getInt(failure) == 17;
                }

                // API28's legacy daemon connector wraps fixed-operation EEXIST in
                // IllegalStateException.
                if (failure instanceof IllegalStateException && failure.getMessage() != null) {
                    exists = failure.getMessage().contains("(File exists)");
                }

                if (!exists) {
                    throw wrapped;
                }
            }
        }

        // The platform class is resolved by name; a mismatch fails as before.
        @SuppressWarnings("UnsafeReflectiveConstructionCast")
        @Override
        public void requestRoutes(String iface) throws Exception {
            // A dedicated physical network keeps this NIC out of Wi-Fi and local-network
            // membership.
            // Legacy routes cover ordinary unbound sockets without replacing the default network.
            Method create = legacyCreate(api);

            if (create != null) {
                idempotent(manager, create, NETWORK_ID, null);
            } else {
                // Android 11 moves physical network creation to the stable netd AIDL in
                // services.jar.
                ClassLoader loader =
                        (ClassLoader)
                                Class.forName("dalvik.system.PathClassLoader")
                                        .getConstructor(String.class, ClassLoader.class)
                                        .newInstance(
                                                "/system/framework/services.jar",
                                                ClassLoader.getSystemClassLoader());
                Class<?> netd = Class.forName("android.net.INetd", false, loader);
                Object endpoint =
                        Class.forName("android.os.ServiceManager")
                                .getMethod("getService", String.class)
                                .invoke(null, "netd");

                if (endpoint == null) {
                    throw new IllegalStateException("netd_unavailable");
                }

                Object daemon =
                        Class.forName("android.net.INetd$Stub", false, loader)
                                .getMethod("asInterface", binder)
                                .invoke(null, endpoint);

                if (!Boolean.TRUE.equals(netd.getMethod("isAlive").invoke(daemon))) {
                    throw new IllegalStateException("netd_unavailable");
                }

                int permission = netd.getField("PERMISSION_NONE").getInt(null);

                if (permission != 0) {
                    throw new IllegalStateException("netd_permission");
                }

                idempotent(
                        daemon,
                        netd.getMethod("networkCreatePhysical", int.class, int.class),
                        NETWORK_ID,
                        permission);
            }

            idempotent(
                    manager,
                    api.getMethod("addInterfaceToNetwork", String.class, int.class),
                    iface,
                    NETWORK_ID);
            Object connected = connected(iface);

            for (int uid : new int[] {0, 10000}) {
                idempotent(
                        manager,
                        api.getMethod("addLegacyRouteForNetId", int.class, route, int.class),
                        NETWORK_ID,
                        connected,
                        uid);
            }
        }
    }

    static int run(String[] args) {
        try {
            if (args.length != 1 || !address(args[0])) {
                throw new IllegalArgumentException("arguments_rejected");
            }

            if (!Integer.valueOf(0)
                    .equals(Class.forName("android.os.Process").getMethod("myUid").invoke(null))) {
                throw new IllegalStateException("root_required");
            }

            if (!Boolean.TRUE.equals(
                    Class.forName("android.os.SELinux")
                            .getMethod("isSELinuxEnforced")
                            .invoke(null))) {
                throw new IllegalStateException("enforcing_required");
            }

            System.out.println(
                    "VOYEMU_NETWORK_V1 " + apply(new BinderManager(args[0]), args[0]).json());

            return 0;
        } catch (Exception failure) {
            // checkstyle-allow IllegalCatch: the process entry reports every failure as its result
            // line
            if (failure instanceof InterruptedException) {
                Thread.currentThread().interrupt();
            }

            String error = failure.getMessage();

            if (!("service_unavailable".equals(error)
                    || "root_required".equals(error)
                    || "enforcing_required".equals(error)
                    || "arguments_rejected".equals(error))) {
                error = "network_setup_failed";
            }

            System.out.println(
                    "VOYEMU_NETWORK_V1 {\"schema\":1,\"ok\":false,\"error\":\"" + error + "\"}");

            return 2;
        }
    }

    public static void main(String[] args) {
        int status = run(args);

        if (status != 0) {
            System.exit(status);
        }
    }

    private NetworkSetup() {}
}
