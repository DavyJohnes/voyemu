package android.hardware.display;

import android.os.Handler;
import android.view.Display;

import java.util.LinkedHashMap;
import java.util.Map;

public class DisplayManager {
    public interface DisplayListener {
        void onDisplayAdded(int id);

        void onDisplayChanged(int id);

        void onDisplayRemoved(int id);
    }

    public final Map<Integer, Display> all = new LinkedHashMap<>();
    DisplayListener listener;

    public Display getDisplay(int id) {
        return all.get(id);
    }

    public Display[] getDisplays() {
        return all.values().toArray(new Display[0]);
    }

    public void registerDisplayListener(DisplayListener l, Handler h) {
        if (listener != null) {
            throw new AssertionError("duplicate listener");
        }

        listener = l;
    }

    public void add(Display d) {
        all.put(d.id, d);

        if (listener != null) {
            listener.onDisplayAdded(d.id);
        }
    }

    public void changed(int id) {
        listener.onDisplayChanged(id);
    }

    public void remove(int id) {
        all.remove(id);
        listener.onDisplayRemoved(id);
    }
}
