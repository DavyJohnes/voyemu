package android.view;

import android.os.Looper;

import java.util.ArrayList;
import java.util.List;

// Records receivers and finished events; deliver plays the role of the input dispatcher.
public abstract class InputEventReceiver {
    public static final List<InputEventReceiver> RECEIVERS = new ArrayList<>();
    public final InputChannel channel;
    public final Looper looper;
    public final List<InputEvent> finished = new ArrayList<>();
    public boolean disposed;

    public InputEventReceiver(InputChannel channel, Looper looper) {
        this.channel = channel;
        this.looper = looper;
        RECEIVERS.add(this);
    }

    public void onInputEvent(InputEvent event) {
        finishInputEvent(event, false);
    }

    public final void finishInputEvent(InputEvent event, boolean handled) {
        if (handled) {
            throw new AssertionError("a monitor never consumes an event");
        }

        finished.add(event);
    }

    public void dispose() {
        disposed = true;
    }

    public void deliver(InputEvent event) {
        if (disposed) {
            throw new AssertionError("event delivered to a disposed receiver");
        }

        onInputEvent(event);
    }
}
