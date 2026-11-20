package android.view;

import android.os.Looper;

// Compile-only API30 hidden signatures. Never merged into guest DEX.
public abstract class InputEventReceiver {
    public InputEventReceiver(InputChannel channel, Looper looper) {}

    public native void onInputEvent(InputEvent event);

    public final native void finishInputEvent(InputEvent event, boolean handled);

    public native void dispose();
}
