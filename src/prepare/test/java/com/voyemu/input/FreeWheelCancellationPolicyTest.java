package com.voyemu.input;

/** Numeric OEM phase boundary only; reflective device invocation is an explicit runtime gate. */
public final class FreeWheelCancellationPolicyTest {
    public static void main(String[] args) {
        if (FreeWheelCancellation.cancelPhase(1) != 0) {
            throw new AssertionError("cancel would emit short");
        }

        for (int phase : new int[] {0, 2, 4, 8}) {
            if (FreeWheelCancellation.cancelPhase(phase) != phase) {
                throw new AssertionError("lost OEM hold/long stop/release");
            }
        }

        System.out.println(
                "Free cancel suppresses only DOWN short-click phase; real reflective invocation"
                        + " unverified");
    }
}
