package com.voyemu.prepare;

import static com.voyemu.prepare.HostTestRunner.jdkTool;
import static com.voyemu.prepare.HostTestRunner.pathOptions;
import static com.voyemu.prepare.HostTestRunner.require;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Map;

/** Compile the real NetworkSetup entrypoint against controlled Android service stubs. */
public final class NetworkSetupReadinessTest {
    private static final String PREFIX = "VOYEMU_NETWORK_V1 ";
    private static final Map<String, String> SOURCES =
            Map.of(
                    "android/os/IBinder.java",
                            """
                            package android.os;
                            public interface IBinder {}
                            """,
                    "android/os/Process.java",
                            """
                            package android.os;
                            public class Process {
                                public static int myUid() { return Integer.getInteger("uid", 0); }
                            }
                            """,
                    "android/os/SELinux.java",
                            """
                            package android.os;
                            public class SELinux {
                                public static boolean isSELinuxEnforced() { return !Boolean.getBoolean("permissive"); }
                            }
                            """,
                    "android/os/ServiceManager.java",
                            """
                            package android.os;
                            public class ServiceManager {
                                static int calls;
                                public static Object getService(String name) {
                                    System.err.println("lookup:" + (++calls));
                                    if (Boolean.getBoolean("reflectfail")) throw new SecurityException("denied");
                                    return calls <= Integer.getInteger("missing", 0) ? null : new IBinder() {};
                                }
                            }
                            """,
                    "android/os/INetworkManagementService.java",
                            """
                            package android.os;
                            import android.net.InterfaceConfiguration;
                            import android.net.RouteInfo;
                            public class INetworkManagementService {
                                public static class Stub {
                                    public static INetworkManagementService asInterface(IBinder binder) {
                                        return new INetworkManagementService();
                                    }
                                }
                                static InterfaceConfiguration value = new InterfaceConfiguration();
                                public String[] listInterfaces() { return new String[] {"eth1"}; }
                                public InterfaceConfiguration getInterfaceConfig(String name) { return value; }
                                public void setInterfaceConfig(String name, InterfaceConfiguration config) {
                                    System.err.println("configured");
                                    value = config;
                                }
                                public void createPhysicalNetwork(int id, String permission) {
                                    if (Boolean.getBoolean("routefail")) throw new IllegalStateException("route_denied");
                                }
                                public void addInterfaceToNetwork(String name, int id) {}
                                public void addLegacyRouteForNetId(int id, RouteInfo route, int uid) {}
                            }
                            """,
                    "android/net/LinkAddress.java",
                            """
                            package android.net;
                            import java.net.InetAddress;
                            public class LinkAddress {
                                final String text;
                                public LinkAddress(String text) { this.text = text; }
                                public InetAddress getAddress() throws Exception {
                                    return InetAddress.getByName(text.split("/")[0]);
                                }
                                public int getPrefixLength() { return 24; }
                            }
                            """,
                    "android/net/InterfaceConfiguration.java",
                            """
                            package android.net;
                            public class InterfaceConfiguration {
                                LinkAddress address;
                                boolean up;
                                public String getHardwareAddress() { return "52:54:00:76:6f:21"; }
                                public void setLinkAddress(LinkAddress value) { address = value; }
                                public LinkAddress getLinkAddress() { return address; }
                                public void setInterfaceUp() { up = true; }
                                public boolean isUp() { return up; }
                            }
                            """,
                    "android/net/RouteInfo.java",
                            """
                            package android.net;
                            import java.net.InetAddress;
                            public class RouteInfo {
                                public RouteInfo(LinkAddress link, InetAddress gateway, String iface) {}
                            }
                            """);

    public static void main(String[] args) throws Exception {
        Path root =
                (args.length == 0 ? Path.of("") : pathOptions(args, "--root").get("--root"))
                        .toRealPath();
        Path source = root.resolve("src/guest/android/api/NetworkSetup.java");

        require(
                Files.isRegularFile(source),
                "Run from the Voyemu repository root or supply --root ROOT");
        Path scratch = root.resolve(".local/build/jobs");
        Files.createDirectories(scratch);
        Path work = Files.createTempDirectory(scratch, "network-readiness-");

        try {
            var compile =
                    new ArrayList<>(
                            List.of(
                                    jdkTool("javac").toString(),
                                    "-d",
                                    work.toString(),
                                    source.toString()));

            for (var entry : SOURCES.entrySet()) {
                Path stub = work.resolve(entry.getKey());
                Files.createDirectories(stub.getParent());
                Files.writeString(stub, entry.getValue());
                compile.add(stub.toString());
            }

            require(
                    HostTestRunner.run(new ProcessBuilder(compile).inheritIO(), 60) == 0,
                    "NetworkSetup compilation failed");

            Result result = run(work, List.of("-Dmissing=2"), List.of());
            require(
                    result.status() == 0
                            && Boolean.TRUE.equals(result.receipt().get("ok"))
                            && result.stderr().contains("lookup:3")
                            && result.stderr().contains("configured"),
                    result.toString());
            System.out.println(
                    "PASS transient null service -> actual configure and verified receipt");

            result = run(work, List.of("-Dmissing=99999"), List.of());
            requireFailure(result, "service_unavailable");
            require(
                    result.seconds() >= 4
                            && result.seconds() < 12
                            && !result.stderr().contains("configured"),
                    result.toString());
            System.out.println(
                    "PASS finite readiness deadline, structured nonfatal nonzero failure");

            var scenarios =
                    List.of(
                            new Scenario(
                                    List.of("-Dreflectfail=true"),
                                    List.of(),
                                    "network_setup_failed"),
                            new Scenario(
                                    List.of("-Droutefail=true"), List.of(), "network_setup_failed"),
                            new Scenario(List.of("-Duid=2000"), List.of(), "root_required"),
                            new Scenario(
                                    List.of("-Dpermissive=true"), List.of(), "enforcing_required"),
                            new Scenario(List.of(), List.of("unexpected"), "arguments_rejected"));

            for (Scenario scenario : scenarios) {
                result = run(work, scenario.properties(), scenario.args());
                requireFailure(result, scenario.error());
                require(result.seconds() < 2, scenario + " exceeded its deadline: " + result);

                if (scenario.properties().contains("-Dreflectfail=true")) {
                    long lookups =
                            result.stderr()
                                    .lines()
                                    .filter(line -> line.startsWith("lookup:"))
                                    .count();
                    require(lookups == 1, result.toString());
                }

                System.out.println(
                        "PASS "
                                + (scenario.properties().isEmpty()
                                        ? scenario.args()
                                        : scenario.properties()));
            }
        } finally {
            List<Path> files;

            try (var paths = Files.walk(work)) {
                files = paths.sorted(Comparator.reverseOrder()).toList();
            }

            for (Path file : files) {
                Files.delete(file);
            }
        }
    }

    private static Result run(Path work, List<String> properties, List<String> args)
            throws Exception {
        var command = new ArrayList<>(List.of(jdkTool("java").toString()));

        command.addAll(properties);
        command.addAll(List.of("-cp", work.toString(), "com.voyemu.api.NetworkSetup"));
        command.addAll(args);
        Path stdout = work.resolve("network.stdout");
        Path stderr = work.resolve("network.stderr");
        long start = System.nanoTime();
        int status =
                HostTestRunner.run(
                        new ProcessBuilder(command)
                                .redirectOutput(stdout.toFile())
                                .redirectError(stderr.toFile()),
                        15);
        double seconds = (System.nanoTime() - start) / 1_000_000_000.0;
        String out = Files.readString(stdout);
        String err = Files.readString(stderr);
        List<String> lines = out.lines().toList();
        require(
                lines.size() == 1 && lines.get(0).startsWith(PREFIX),
                "Invalid NetworkSetup receipt: " + out + err);
        var receipt = Json.object(Json.parse(lines.get(0).substring(PREFIX.length())));
        require(Json.integer(receipt, "schema") == 1, "Unexpected receipt schema: " + receipt);
        require(!err.contains("Exception in thread") && !err.contains("FATAL"), err);

        return new Result(status, err, seconds, receipt);
    }

    private static void requireFailure(Result result, String error) {
        require(
                result.status() == 2
                        && Boolean.FALSE.equals(result.receipt().get("ok"))
                        && error.equals(result.receipt().get("error")),
                "Expected " + error + ": " + result);
    }

    private record Scenario(List<String> properties, List<String> args, String error) {}

    private record Result(int status, String stderr, double seconds, Map<String, Object> receipt) {}
}
