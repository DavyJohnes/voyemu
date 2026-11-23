package com.qinggan.canbus.service;

import android.content.Context;
import android.os.Handler;

import java.util.ArrayList;
import java.util.List;

/** Full host power follows successful persistence of the current complete input revision. */
public final class HostAccInputsTest {
    private static final class Component extends BaseCanBusComponent {
        final List<Integer> acc = new ArrayList<>();
        final List<Long> sequences = new ArrayList<>();

        @Override
        public void setHostAccStatus(int value) {
            if (state().requestHostAcc(value)) {
                acc.add(value);
                sequences.add(state().inputs().sequence);
            }
        }
    }

    private static void check(boolean value, String message) {
        CanBusComponentTest.check(value, message);
    }

    public static void main(String[] args) throws Exception {
        Context context = new Context();
        Component component = new Component();

        component.init(context);
        Handler.drain();
        CanBusState state = component.state();
        String first = CanBusComponentTest.inputs(1, 0, 0);
        check(state.applyVehicleInputs(first, 0), "complete OFF rejected");
        check(component.acc.isEmpty(), "ACC requested before input persistence");
        Handler.drain();
        check(component.acc.equals(List.of(0)), "persisted OFF not requested");
        check(component.sequences.equals(List.of(1L)), "ACC used uncommitted revision");
        check(state.applyVehicleInputs(first, 0), "same full state rejected");
        Handler.drain();
        check(component.acc.equals(List.of(0)), "duplicate OFF requested twice");
        check(!state.applyVehicleInputs("{}", 2), "malformed host state accepted");
        check(
                !state.applyVehicleInputs(CanBusComponentTest.inputs(1, 0, 1), 2),
                "conflicting revision accepted");
        check(!state.applyVehicleInputs(first, 1), "unknown ACC accepted");
        Handler.drain();
        check(component.acc.equals(List.of(0)), "rejected input requested ON");
        check(
                state.applyVehicleInputs(CanBusComponentTest.inputs(2, 0, 0), 2),
                "next full state rejected");
        check(
                state.applyVehicleInputs(CanBusComponentTest.inputs(3, 0, 0), 0),
                "latest OFF rejected");
        Handler.drain();
        check(component.acc.equals(List.of(0)), "superseded revision requested ON");
        check(!state.applyVehicleInputs(first, 2), "stale full state accepted");
        Handler.drain();
        check(component.acc.equals(List.of(0)), "stale state requested ON");
        context.getSharedPreferences("voyemu-vehicle-input", Context.MODE_PRIVATE).failCommits =
                true;
        check(
                state.applyVehicleInputs(CanBusComponentTest.inputs(4, 0, 0), 2),
                "valid revision should reach persistence");
        Handler.drain();
        check(
                component.acc.equals(List.of(0)) && state.inputs().sequence == 3,
                "failed persistence requested ON or published its revision");
        context.getSharedPreferences("voyemu-vehicle-input", Context.MODE_PRIVATE).failCommits =
                false;
        check(
                state.applyVehicleInputs(CanBusComponentTest.inputs(4, 0, 0), 2),
                "retry after failed write rejected");
        Handler.drain();
        check(component.acc.equals(List.of(0, 2)), "accepted READY did not request ON");
        check(component.sequences.equals(List.of(1L, 4L)), "power revision mismatch");
        System.out.println(
                "host ACC persistence, current revisions, duplicate and failures passed");
    }
}
