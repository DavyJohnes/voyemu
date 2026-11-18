package android.hardware.display;

import android.view.Display;

public class DisplayManager {
    public boolean available = true;

    public Display[] getDisplays() {
        return available
                ? new Display[] {new Display(0), new Display(7)}
                : new Display[] {new Display(0)};
    }

    public Display getDisplay(int id) {
        return available && id == 7 ? new Display(id) : null;
    }
}
