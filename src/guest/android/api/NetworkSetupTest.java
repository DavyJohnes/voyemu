package com.voyemu.api;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public final class NetworkSetupTest {
    static final class Fake implements NetworkSetup.Manager {
        String[] names = {"lo", "wlan0", "eth1"};
        boolean configured, changed, failRoute;
        String selected;
        final List<String> calls = new ArrayList<>();

        @Override
        public String[] interfaces() {
            return names;
        }

        @Override
        public String hardware(String name) {
            return name.startsWith("eth") ? NetworkSetup.MAC : "00:00:00:00:00:00";
        }

        @Override
        public void configure(String name) {
            selected = name;
            configured = true;
            calls.add("configure:" + name);
        }

        @Override
        public boolean configured(String name) {
            return configured && !changed;
        }

        @Override
        public void requestRoutes(String name) {
            if (failRoute) {
                throw new IllegalStateException("denied");
            }

            calls.add("fixed-routes:" + name);
        }
    }

    public static final class LegacyApi {
        public void createPhysicalNetwork(int network, String permission) {}
    }

    public static final class ModernApi {}

    static void check(boolean condition) {
        if (!condition) {
            throw new AssertionError();
        }
    }

    static void reject(Fake fake, String reason) throws Exception {
        try {
            NetworkSetup.apply(fake);
            throw new AssertionError("accepted " + reason);
        } catch (IllegalStateException expected) {
            check(reason.equals(expected.getMessage()));
        }
    }

    public static void main(String[] args) throws Exception {
        check(NetworkSetup.legacyCreate(LegacyApi.class) != null);
        check(NetworkSetup.legacyCreate(ModernApi.class) == null);
        check(NetworkSetup.legacyCreate(LegacyApi.class).getParameterTypes()[1] == String.class);
        Fake fake = new Fake();
        NetworkSetup.Result result = NetworkSetup.apply(fake);
        check("eth1".equals(result.iface));
        check(result.networks.equals(Arrays.asList(65000)));
        check(fake.calls.equals(Arrays.asList("configure:eth1", "fixed-routes:eth1")));
        check(
                result.json().contains("\"local_route_requested\":true")
                        && !result.json().contains("\"local_route\":")
                        && result.json().contains("\"address\":\"172.16.100.21\"")
                        && result.json().contains("\"network_ids\":[65000]"));
        fake = new Fake();
        fake.names = new String[] {"wlan0"};
        reject(fake, "interface_missing");
        check(fake.calls.isEmpty());
        fake = new Fake();
        fake.names = new String[] {"eth1", "eth2"};
        reject(fake, "interface_ambiguous");
        check(fake.calls.isEmpty());
        fake = new Fake();
        fake.names = new String[] {"../eth1"};
        reject(fake, "interface_name");
        fake = new Fake();
        fake.changed = true;
        reject(fake, "interface_readback");
        check(fake.calls.equals(Arrays.asList("configure:eth1")));
        fake = new Fake();
        fake.failRoute = true;
        reject(fake, "denied");
        check(NetworkSetup.run(new String[] {"172.16.100.1"}) == 2);
        System.out.println(
                "Fixed NIC identity, fixed private network, scoped route ownership and fail-closed"
                        + " readbacks passed");
    }
}
