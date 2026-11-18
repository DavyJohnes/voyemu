package android.inputmethodservice;

import android.app.Dialog;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.view.WindowManager;

import java.util.Map;

public final class InputMethodService extends Context {
    public Dialog dialog;

    public InputMethodService(DisplayManager displays, Map<Integer, WindowManager> managers) {
        super("test.ime", displays, managers);
    }

    public Dialog getWindow() {
        return dialog;
    }
}
