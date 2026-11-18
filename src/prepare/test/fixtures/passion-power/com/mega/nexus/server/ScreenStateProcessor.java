package com.mega.nexus.server;

import java.util.Stack;

/** Screen policy state for canonical-screen admission and restoration tests. */
public final class ScreenStateProcessor {
    public int mScreenId;
    public boolean mScreenOnTarget = true;
    public final Stack<String> mOnStateCtrlList = new Stack<>();
    public final Stack<String> mOffStateCtrlList = new Stack<>();
    public int reversals;

    public void syncScreenState(boolean on) {
        if (mScreenOnTarget != on) {
            throw new AssertionError("reverse notification must follow observed state");
        }

        reversals++;
    }
}
