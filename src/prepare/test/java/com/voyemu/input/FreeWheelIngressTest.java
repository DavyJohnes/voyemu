package com.voyemu.input;

import android.app.ActivityManager;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ProviderInfo;
import android.os.Binder;
import android.util.Log;

import com.qinggan.keymanager.service.engine.KeyManagerEngine;
import com.qinggan.keymanager.service.engine.KeyManagerMessage;
import com.qinggan.keymanager.service.engine.KeyManagerReader;
import com.qinggan.system.QGKeyEvent;

import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;

/** Actual helper with own Android/dispatch doubles. No OEM phase/timer implementation is copied. */
public final class FreeWheelIngressTest {
    private static final String OWN = "com.voyemu.input.wheel",
            SERVICE = "com.qinggan.keymanager.service";

    private static void check(boolean value, String text) {
        if (!value) {
            throw new AssertionError(text);
        }
    }

    private static final class Packages extends PackageManager {
        ProviderInfo provider;
        int signatures, reads;
        boolean alterBinder, unavailable;

        public ProviderInfo resolveContentProvider(String name, int flags) {
            reads++;

            if (unavailable) {
                throw new SecurityException("unavailable");
            }

            if (alterBinder) {
                Binder.uid = 2000;
                Binder.pid = 99;
            }

            check(OWN.equals(name) && flags == 0, "exact authority query");

            return provider;
        }

        public int checkSignatures(String first, String second) {
            check(SERVICE.equals(first) && OWN.equals(second), "exact signer query");

            return signatures;
        }
    }

    private static final class Manager extends ActivityManager {
        List<RunningAppProcessInfo> rows = new ArrayList<>();

        public List<RunningAppProcessInfo> getRunningAppProcesses() {
            return rows;
        }
    }

    private static final class TestContext extends Context {
        Packages packages = new Packages();
        Manager manager = new Manager();
        String name = SERVICE;

        public String getPackageName() {
            return name;
        }

        public PackageManager getPackageManager() {
            return packages;
        }

        public Object getSystemService(String service) {
            check(ACTIVITY_SERVICE.equals(service), "only process metadata");

            return manager;
        }
    }

    private static final class Fixture {
        TestContext context = new TestContext();
        KeyManagerReader reader = new KeyManagerReader();
        KeyManagerEngine engine = new KeyManagerEngine(context, reader);
        KeyManagerMessage message = new KeyManagerMessage();

        Fixture() {
            Binder.uid = 1000;
            Binder.pid = 77;
            Binder.uidReads = Binder.pidReads = 0;
            Log.LINES.clear();
            ProviderInfo p = new ProviderInfo();
            p.authority = p.packageName = p.processName = OWN;
            p.name = "com.voyemu.input.FreeKeyInputProvider";
            p.readPermission = p.writePermission = "android.permission.DUMP";
            p.enabled = p.exported = true;
            p.applicationInfo = new ApplicationInfo();
            p.applicationInfo.uid = 1000;
            p.applicationInfo.enabled = true;
            p.applicationInfo.packageName = p.applicationInfo.processName = OWN;
            context.packages.provider = p;
            ActivityManager.RunningAppProcessInfo process =
                    new ActivityManager.RunningAppProcessInfo();
            process.pid = 77;
            process.uid = 1000;
            process.processName = OWN;
            process.pkgList = new String[] {OWN};
            context.manager.rows.add(process);
            message.keyEvent = new QGKeyEvent(2, 4096, 0);
        }

        boolean route() {
            return FreeWheelIngress.route(engine, message);
        }

        void own() {
            check(
                    route()
                            && reader.calls == 1
                            && reader.event == message.keyEvent
                            && engine.legacyCalls == 0,
                    "own phase must reach original reader exactly once");
            check(Binder.uidReads == 1 && Binder.pidReads == 1, "caller captured once at ingress");
        }

        void legacy() {
            check(
                    route()
                            && reader.calls == 0
                            && engine.legacyCalls == 1
                            && engine.legacyMessage == message,
                    "foreign dispatch must remain original");
            check(Log.LINES.isEmpty(), "no authenticated reader claim for foreign caller");
        }
    }

    private static void legacy(Consumer<Fixture> change) {
        Fixture f = new Fixture();

        change.accept(f);
        f.legacy();
    }

    private static void refused(Consumer<Fixture> change) {
        Fixture f = new Fixture();

        change.accept(f);

        try {
            f.route();
            throw new AssertionError("invalid own boundary accepted");
        } catch (IllegalArgumentException | IllegalStateException expected) {
        }

        check(
                f.reader.calls == 0 && f.engine.legacyCalls == 0,
                "invalid own event partially dispatched");
    }

    public static void main(String[] args) {
        new Fixture().own();

        for (int[] event :
                new int[][] {{2, 4096, 0}, {2, 65536, 0}, {1, 4096, 0}, {1, 65536, 32}}) {
            Fixture f = new Fixture();
            f.message.keyEvent = new QGKeyEvent(event[0], event[1], event[2]);
            f.own();
            check(
                    f.reader.event.getAction() == event[1] && f.reader.event.getFlags() == event[2],
                    "no phase/cancel synthesis");
        }

        Fixture captured = new Fixture();
        captured.context.packages.alterBinder = true;
        captured.own();
        check(Binder.uid == 2000, "outbound IPC simulation exercised");
        legacy(f -> f.message.mCallingPackageName = "foreign.package");
        legacy(f -> f.message.mCallingPackageName = null);
        legacy(f -> Binder.uid = 2000);
        legacy(f -> Binder.pid = 500);
        legacy(f -> Binder.pid = 78);
        legacy(f -> f.context.manager.rows.get(0).uid = 2000);
        legacy(f -> f.context.manager.rows.get(0).processName = "foreign.process");
        legacy(f -> f.context.manager.rows.get(0).pkgList = new String[] {OWN, "foreign.package"});
        legacy(f -> f.context.manager.rows.get(0).pkgList = null);
        legacy(f -> f.context.manager.rows.add(f.context.manager.rows.get(0)));
        legacy(f -> f.context.manager.rows.clear());
        legacy(f -> f.context.manager.rows = null);
        legacy(f -> f.context.packages.signatures = -1);
        legacy(f -> f.context.packages.provider.authority = "foreign");
        legacy(f -> f.context.packages.provider.name = "foreign.Provider");
        legacy(f -> f.context.packages.provider.readPermission = null);
        legacy(f -> f.context.packages.provider.writePermission = "foreign.permission");
        legacy(f -> f.context.packages.provider.applicationInfo.uid = 2000);
        legacy(f -> f.context.packages.provider.enabled = false);
        legacy(f -> f.context.packages.provider.exported = false);
        legacy(f -> f.context.packages.provider.applicationInfo.enabled = false);
        legacy(f -> f.context.packages.provider = null);
        legacy(f -> f.context.packages.unavailable = true);
        Fixture foreign = new Fixture();
        foreign.message.mCallingPackageName = "foreign";
        foreign.engine.result = false;
        check(
                !foreign.route()
                        && foreign.engine.legacyCalls == 1
                        && foreign.context.packages.reads == 0,
                "legacy false and no auth IPC preserved");
        Fixture falseReader = new Fixture();
        falseReader.reader.result = false;
        check(
                !falseReader.route()
                        && falseReader.reader.calls == 1
                        && falseReader.engine.legacyCalls == 0,
                "no fabricated reader success");
        Fixture failed = new Fixture();
        RuntimeException original = new IllegalArgumentException("original-reader-error");
        failed.reader.failure = original;

        try {
            failed.route();
            throw new AssertionError("lost original exception");
        } catch (RuntimeException actual) {
            check(actual == original, "original reader exception must survive");
        }

        refused(f -> f.message.mWhat = 16);
        refused(f -> f.message.keyEvent = null);
        refused(f -> f.message.keyEvent = new QGKeyEvent(0, 4096, 0));
        refused(f -> f.message.keyEvent = new QGKeyEvent(2, 8192, 0));
        refused(f -> f.message.keyEvent = new QGKeyEvent(2, 4096, 32));
        refused(f -> f.message.keyEvent = new QGKeyEvent(2, 65536, 64));
        refused(f -> f.engine.modules(List.of()));
        refused(f -> f.engine.modules(null));
        refused(f -> f.engine.modules(List.of(new Object())));
        refused(f -> f.engine.modules(List.of(f.reader, new KeyManagerReader())));
        System.out.println(
                "Actual ingress helper: original reader for authenticated UID/PID/provider only;"
                    + " foreign original dispatch; early caller snapshot; unchanged DOWN/UP/cancel;"
                    + " original false/errors; invalid events no partial dispatch PASS. OEM"
                    + " classifier/timer execution remains a guest gate.");
    }
}
