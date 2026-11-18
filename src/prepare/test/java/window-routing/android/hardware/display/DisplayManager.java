package android.hardware.display;

import android.os.Handler;
import android.view.Display;

import java.util.LinkedHashMap;
import java.util.Map;

public class DisplayManager {
    public final Map<Integer, Display> live = new LinkedHashMap<>();
    DisplayListener listener;

    public Display[] getDisplays() {
        return live.values().toArray(new Display[0]);
    }

    public Display getDisplay(int id) {
        return live.get(id);
    }

    public void registerDisplayListener(DisplayListener l, Handler h) {
        listener = l;
    }

    public void add(int id, String name) {
        live.put(id, new Display(id, name));

        if (listener != null) {
            listener.onDisplayAdded(id);
        }
    }

    public void remove(int id) {
        live.remove(id);
        listener.onDisplayRemoved(id);
    }

    public interface DisplayListener {
        void onDisplayAdded(int id);

        void onDisplayRemoved(int id);

        void onDisplayChanged(int id);
    }
}
