package android.hardware.input;

import android.view.InputMonitor;

import java.util.ArrayList;
import java.util.List;

public class InputManager {
    public final List<InputMonitor> monitors = new ArrayList<>();

    public InputMonitor monitorGestureInput(String name, int display) {
        var monitor = new InputMonitor(name, display);

        monitors.add(monitor);

        return monitor;
    }
}
