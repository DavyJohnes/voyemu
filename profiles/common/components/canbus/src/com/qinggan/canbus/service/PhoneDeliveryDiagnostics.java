package com.qinggan.canbus.service;

import java.util.LinkedHashMap;
import java.util.Map;

/** Bounded process-local receipt of the normal PhoneInfo path, without phone data. */
final class PhoneDeliveryDiagnostics {
    enum Stage {
        SERVICE_CREATED,
        FRAMEWORK_LOAD,
        MANAGER,
        ADAPTER,
        SNAPSHOT_METHOD,
        CALLBACK,
        ATTACHED,
        CLOSED
    }

    enum Failure {
        NONE,
        ATTACH_REFLECTION,
        ATTACH_RUNTIME,
        CALLBACK_ARGUMENTS,
        METADATA_SHAPE,
        METADATA_INVOCATION,
        METADATA_VALUES,
        SINK_RUNTIME,
        SINK_VALUES,
        SOCKET_NOT_SUBMITTED,
        UNSUBSCRIBE
    }

    enum Event {
        ATTACH_ATTEMPT,
        ATTACH_SUCCESS,
        CALLBACK_SEEN,
        CALLBACK_REJECTED,
        METADATA_ACCEPTED,
        METADATA_REJECTED,
        DUPLICATE,
        SINK_ATTEMPT,
        SINK_RETURNED,
        SINK_RECEIVED,
        SINK_REJECTED,
        ENQUEUED,
        NO_FRAMES,
        SOCKET_SUBMITTED,
        SOCKET_FAILED,
        CLOSED
    }

    private final int[] counts = new int[Event.values().length];
    private Stage stage = Stage.SERVICE_CREATED;
    private Failure firstFailure = Failure.NONE, lastFailure = Failure.NONE;
    private int lastCallState = -1;
    private IpkLink.Stage socketStage = IpkLink.Stage.UNKNOWN;
    private IpkLink.Reason socketReason = IpkLink.Reason.NONE;
    private IpkLink.IoClass socketClass = IpkLink.IoClass.NONE;

    synchronized void socket(
            boolean submitted,
            IpkLink.Stage stage,
            IpkLink.Reason reason,
            IpkLink.IoClass ioClass) {
        socketStage = stage;
        socketReason = reason;
        socketClass = ioClass;
        event(submitted ? Event.SOCKET_SUBMITTED : Event.SOCKET_FAILED);

        if (!submitted) {
            failure(Failure.SOCKET_NOT_SUBMITTED);
        }
    }

    synchronized void stage(Stage value) {
        stage = value;
    }

    synchronized void event(Event value) {
        int i = value.ordinal();

        if (counts[i] < Integer.MAX_VALUE) {
            counts[i]++;
        }
    }

    synchronized void failure(Failure value) {
        if (firstFailure == Failure.NONE) {
            firstFailure = value;
        }

        lastFailure = value;
    }

    synchronized void callback(int state) {
        event(Event.CALLBACK_SEEN);
        lastCallState = state >= 128 && state <= 131 ? state : -1;
    }

    synchronized Map<String, Object> snapshot() {
        Map<String, Object> output = new LinkedHashMap<>(), events = new LinkedHashMap<>();

        for (Event event : Event.values()) {
            events.put(event.name().toLowerCase(java.util.Locale.ROOT), counts[event.ordinal()]);
        }

        output.put("stage", stage.name().toLowerCase(java.util.Locale.ROOT));
        output.put("first_failure", firstFailure.name().toLowerCase(java.util.Locale.ROOT));
        output.put("last_failure", lastFailure.name().toLowerCase(java.util.Locale.ROOT));
        output.put("last_call_state", lastCallState);
        output.put("counts", events);
        output.put("socket_stage", socketStage.name().toLowerCase(java.util.Locale.ROOT));
        output.put("socket_reason", socketReason.name().toLowerCase(java.util.Locale.ROOT));
        output.put("socket_io_class", socketClass.name().toLowerCase(java.util.Locale.ROOT));

        return output;
    }
}
