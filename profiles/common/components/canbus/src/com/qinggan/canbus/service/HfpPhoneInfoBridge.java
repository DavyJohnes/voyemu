package com.qinggan.canbus.service;

import android.content.Context;
import android.os.Bundle;
import android.os.SystemClock;
import android.util.Log;

import com.qinggan.canbus.PhoneInfo;
import com.qinggan.canbus.PhoneState;

import java.lang.reflect.Method;
import java.lang.reflect.Proxy;

/** Owned HFP cluster publisher using OEM callback ABI and our adapter snapshot extension. */
final class HfpPhoneInfoBridge implements AutoCloseable {
    private final Context context;
    private final CanBusStubService sink;
    private final PhoneDeliveryDiagnostics diagnostics;
    private Object adapter, hfp, callback;
    private Method unregister, snapshot;
    private boolean closed;
    private int lastState = 131;
    private String lastNumber = "", lastIncarnation;

    HfpPhoneInfoBridge(
            Context context, CanBusStubService sink, PhoneDeliveryDiagnostics diagnostics) {
        this.context = context;
        this.sink = sink;
        this.diagnostics = diagnostics;
    }

    void start() {
        if (closed || callback != null) {
            return;
        }

        diagnostics.event(PhoneDeliveryDiagnostics.Event.ATTACH_ATTEMPT);

        try {
            diagnostics.stage(PhoneDeliveryDiagnostics.Stage.FRAMEWORK_LOAD);
            ClassLoader loader = context.getClassLoader();
            Class<?> managerClass = Class.forName("com.qinggan.bluetooth.BTManager", true, loader);
            Class<?> adapterClass =
                    Class.forName("com.qinggan.bluetooth.profile.BTAdapter", true, loader);
            Class<?> btCallback =
                    Class.forName("com.qinggan.bluetooth.profile.IBTCallback", true, loader);
            Class<?> hfpClass = Class.forName("com.qinggan.bluetooth.profile.Hfp", true, loader);
            Class<?> hfpCallback =
                    Class.forName("com.qinggan.bluetooth.profile.IHfpCallback", true, loader);
            diagnostics.stage(PhoneDeliveryDiagnostics.Stage.MANAGER);
            Object manager =
                    managerClass.getMethod("getInstance", Context.class).invoke(null, context);
            diagnostics.stage(PhoneDeliveryDiagnostics.Stage.ADAPTER);
            adapter =
                    managerClass
                            .getMethod("getAdapter", btCallback)
                            .invoke(manager, new Object[] {null});
            diagnostics.stage(PhoneDeliveryDiagnostics.Stage.SNAPSHOT_METHOD);
            snapshot = adapter.getClass().getMethod("phoneInfoSnapshot");

            if (snapshot.getReturnType() != Bundle.class) {
                throw new IllegalStateException("PHONE_INFO_ABI");
            }

            unregister = hfpClass.getMethod("unregisterListener", hfpCallback);
            diagnostics.stage(PhoneDeliveryDiagnostics.Stage.CALLBACK);
            callback =
                    Proxy.newProxyInstance(
                            hfpCallback.getClassLoader(),
                            new Class<?>[] {hfpCallback},
                            this::onHfpCallback);
            hfp = adapterClass.getMethod("getHfp", hfpCallback).invoke(adapter, callback);
            diagnostics.event(PhoneDeliveryDiagnostics.Event.ATTACH_SUCCESS);
            diagnostics.stage(PhoneDeliveryDiagnostics.Stage.ATTACHED);
        } catch (ReflectiveOperationException | RuntimeException failure) {
            // checkstyle-allow IllegalCatch: an OEM framework failure is a recorded attach failure
            diagnostics.failure(
                    failure instanceof ReflectiveOperationException
                            ? PhoneDeliveryDiagnostics.Failure.ATTACH_REFLECTION
                            : PhoneDeliveryDiagnostics.Failure.ATTACH_RUNTIME);
            Log.w("CanBusStub", "HFP PhoneInfo subscription unavailable");
            close();
        }
    }

    private Object onHfpCallback(Object proxy, Method method, Object[] args) {
        if (method.getDeclaringClass() == Object.class) {
            if ("equals".equals(method.getName())) {
                return proxy == args[0];
            }

            if ("hashCode".equals(method.getName())) {
                return System.identityHashCode(proxy);
            }

            return "CanBusHfpCallback";
        }

        if (!closed && "on3WayCallStateChanged".equals(method.getName())) {
            diagnostics.callback(
                    args != null && args.length > 0 && args[0] instanceof Integer
                            ? (Integer) args[0]
                            : -1);

            if (args != null
                    && args.length == 3
                    && args[0] instanceof Integer
                    && args[1] instanceof Integer
                    && ((Integer) args[1]) == 1
                    && args[2] instanceof String) {
                publish((Integer) args[0], (String) args[2]);
            } else {
                diagnostics.event(PhoneDeliveryDiagnostics.Event.CALLBACK_REJECTED);
                diagnostics.failure(PhoneDeliveryDiagnostics.Failure.CALLBACK_ARGUMENTS);
            }
        }

        return null;
    }

    private void publish(int state, String number) {
        PhoneDeliveryDiagnostics.Failure failure =
                PhoneDeliveryDiagnostics.Failure.METADATA_INVOCATION;

        try {
            Bundle value = (Bundle) snapshot.invoke(adapter);
            failure = PhoneDeliveryDiagnostics.Failure.METADATA_SHAPE;

            if (value == null
                    || !value.keySet()
                            .equals(
                                    new java.util.HashSet<>(
                                            java.util.Arrays.asList(
                                                    "schema",
                                                    "state",
                                                    "number",
                                                    "name",
                                                    "active_started_elapsed_ms",
                                                    "connected",
                                                    "incarnation")))
                    || !Integer.valueOf(1).equals(value.get("schema"))
                    || !(value.get("state") instanceof Integer)
                    || !(value.get("number") instanceof String)
                    || !(value.get("name") instanceof String)
                    || !(value.get("active_started_elapsed_ms") instanceof Long)
                    || !(value.get("connected") instanceof Boolean)
                    || (value.get("incarnation") != null
                            && !(value.get("incarnation") instanceof String))) {
                throw new IllegalArgumentException("PHONE_INFO_SNAPSHOT");
            }

            failure = PhoneDeliveryDiagnostics.Failure.METADATA_VALUES;
            HfpPhoneInfoValues mapped =
                    HfpPhoneInfoValues.from(
                            state,
                            number,
                            new HfpPhoneInfoValues.Snapshot(
                                    (Integer) value.get("state"),
                                    (String) value.get("number"),
                                    (String) value.get("name"),
                                    (Long) value.get("active_started_elapsed_ms"),
                                    (Boolean) value.get("connected"),
                                    (String) value.get("incarnation")),
                            SystemClock.elapsedRealtime());
            diagnostics.event(PhoneDeliveryDiagnostics.Event.METADATA_ACCEPTED);
            String epoch = (String) value.get("incarnation");

            if (lastState == state
                    && (state == 131
                            || (lastNumber.equals(number)
                                    && java.util.Objects.equals(lastIncarnation, epoch)))) {
                diagnostics.event(PhoneDeliveryDiagnostics.Event.DUPLICATE);

                return;
            }

            PhoneInfo info = new PhoneInfo();
            info.setPhoneState(PhoneState.valueOf(mapped.state));
            info.setDuration(mapped.duration);
            info.setName(mapped.name);
            info.setPhoneNum(mapped.number);
            failure = PhoneDeliveryDiagnostics.Failure.SINK_RUNTIME;
            diagnostics.event(PhoneDeliveryDiagnostics.Event.SINK_ATTEMPT);
            sink.updatePhoneInfo(info);
            diagnostics.event(PhoneDeliveryDiagnostics.Event.SINK_RETURNED);
            lastState = state;
            lastNumber = number;
            lastIncarnation = epoch;
        } catch (ReflectiveOperationException | RuntimeException rejected) {
            // checkstyle-allow IllegalCatch: an OEM or sink failure is a recorded rejected
            // publication
            diagnostics.event(
                    failure == PhoneDeliveryDiagnostics.Failure.SINK_RUNTIME
                            ? PhoneDeliveryDiagnostics.Event.SINK_REJECTED
                            : PhoneDeliveryDiagnostics.Event.METADATA_REJECTED);
            diagnostics.failure(failure);
            Log.w("CanBusStub", "HFP PhoneInfo publication rejected");
        }
    }

    @Override
    public void close() {
        if (closed) {
            return;
        }

        closed = true;
        diagnostics.event(PhoneDeliveryDiagnostics.Event.CLOSED);

        if (hfp != null && callback != null) {
            try {
                unregister.invoke(hfp, callback);
            } catch (ReflectiveOperationException | RuntimeException failure) {
                // checkstyle-allow IllegalCatch: an OEM framework failure is a recorded unsubscribe
                // failure
                diagnostics.failure(PhoneDeliveryDiagnostics.Failure.UNSUBSCRIBE);
                Log.w("CanBusStub", "HFP PhoneInfo unsubscribe failed");
            }
        }

        callback = null;
        hfp = null;
        adapter = null;
    }
}
