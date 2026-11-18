package android.hardware.display;

import android.view.Display;

import java.util.ArrayList;
import java.util.List;

public final class DisplayManager {
    public final List<Display> displays = new ArrayList<>();

    public Display[] getDisplays() {
        return displays.toArray(new Display[0]);
    }
}
