package fixture;

import android.content.Context;
import android.os.Handler;

import com.qinggan.bluetooth.bean.BTCallLogItem;
import com.qinggan.bluetooth.bean.BTContactItem;
import com.qinggan.bluetooth.internal.ivt8.Ivt8BTAdapter;
import com.qinggan.bluetooth.profile.BTDevice;
import com.qinggan.bluetooth.profile.Pbap;
import com.qinggan.bluetooth.profile.SimplePbapCallback;

import java.util.ArrayList;
import java.util.List;

public final class PhonePbapRaceProbe {
    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    public static void run(String scenario, ClassLoader app) throws Exception {
        Context.applicationLoader = app;
        android.os.PeerTestRuntime.enter("phone");
        Class<?> type =
                app.loadClass("com.qinggan.bluetoothphone.logic.manager.CacheCallDataManager");
        Object manager = type.getMethod("getInstance").invoke(null);
        check(
                type.getClassLoader() != PhonePbapRaceProbe.class.getClassLoader(),
                "app manager must be invisible to adapter loader");

        try {
            PhonePbapRaceProbe.class.getClassLoader().loadClass(type.getName());
            throw new AssertionError("adapter loader accidentally sees OEM app manager");
        } catch (ClassNotFoundException expected) {
        }

        Ivt8BTAdapter adapter = Ivt8BTAdapter.getInstance(new Context(), null);
        final List<String> fetched = new ArrayList<>();
        final boolean[] history = {true};
        final int[] ack = {0};
        Pbap pbap =
                adapter.getPbap(
                        new SimplePbapCallback() {
                            public void onReady() {}

                            public void onStateChanged(BTDevice d, int p, int n) {}

                            public void onContactItemCountUpdated(int p, int n) {
                                check(n == 2, "two contacts");
                            }

                            public void onContactItemFetched(BTContactItem item) {
                                check(getSyncDevice() != null, "device context");
                                fetched.add(item.name);
                            }

                            public void onCallLogItemCountUpdated(int p, int n) {
                                check(p == 0 && n == 0, "empty history preserved");
                            }

                            public void onCallLogItemFetched(BTCallLogItem item) {
                                throw new AssertionError("invented call history");
                            }

                            public void onSyncStateChanged(int p, int n) {
                                if (n != 97) {
                                    return;
                                }

                                ack[0]++;

                                try {
                                    if (history[0]) {
                                        history[0] = false;
                                        type.getMethod("insertCallLog").invoke(manager);
                                        adapter.getPbap(null).startContactsSync();
                                    } else {
                                        type.getMethod("insertContacts", List.class)
                                                .invoke(manager, new ArrayList<>(fetched));
                                    }
                                } catch (Exception failure) {
                                    throw new IllegalStateException(failure);
                                }
                            }
                        });
        Handler.drain();
        adapter.connect("02:00:00:00:00:01");
        Handler.drain();

        if ("direct".equals(scenario)) {
            history[0] = false;
            pbap.startContactsSync();
            Handler.drain();
            type.getMethod("runContacts").invoke(manager);
            check(
                    ((Integer) type.getMethod("rows").invoke(manager)) == 2,
                    "direct contacts unchanged");

            return;
        }

        check(pbap.startCallLogSyncByRange(0, 200), "history accepted");
        Handler.drain();
        check(
                (Boolean) type.getMethod("workerRunning").invoke(manager),
                "OEM true written inline before queued contact barrier");
        type.getMethod("runContacts").invoke(manager);
        System.out.println(
                "before history cleanup rows="
                        + type.getMethod("rows").invoke(manager)
                        + " fetched="
                        + fetched.size()
                        + " ack="
                        + ack[0]);

        if ("cancel".equals(scenario)) {
            check(pbap.cancelSync(), "cancel accepted");
            Handler.advance(25);
            check(fetched.isEmpty(), "old pending contacts survived cancel");
            pbap.startContactsSync();
            Handler.drain();
        }

        if ("reconnect".equals(scenario)) {
            pbap.disconnect("02:00:00:00:00:01");
            Handler.drain();
            pbap.connect("02:00:00:00:00:01");
            Handler.drain();
            pbap.startContactsSync();
            Handler.drain();
        }

        if ("timeout".equals(scenario)) {
            Handler.advance(10001);
            check(fetched.isEmpty(), "timeout emitted contacts");
            type.getMethod("workerFinalDelete").invoke(manager);
            Handler.advance(25);
            check(fetched.isEmpty(), "late worker completion revived expired contacts");

            return;
        }

        type.getMethod("workerFirstDelete").invoke(manager);
        Handler.advance(25);
        boolean early = !fetched.isEmpty();
        System.out.println(
                "after first delete worker="
                        + type.getMethod("workerRunning").invoke(manager)
                        + " early_contacts="
                        + early);
        type.getMethod("workerFinalDelete").invoke(manager);
        Handler.advance(25);
        type.getMethod("runContacts").invoke(manager);
        int rows = (Integer) type.getMethod("rows").invoke(manager);
        System.out.println(
                "after both workers rows="
                        + rows
                        + " history="
                        + type.getMethod("historyRows").invoke(manager));
        check(rows == 2, "late OEM empty-calllog cleanup deleted imported contacts");
        check(!early, "contacts emitted before call-log worker quiesced");
        check(ack[0] == 2, "history and contacts each ACK97 exactly once");
    }
}
