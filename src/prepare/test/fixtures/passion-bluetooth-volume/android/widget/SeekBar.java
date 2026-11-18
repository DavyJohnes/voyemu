package android.widget;

import android.content.Context;

public class SeekBar {
    public int max = 9, maxWrites;
    public boolean enabled = true;
    private final Context context;

    public SeekBar(Context c) {
        context = c;
    }

    public Context getContext() {
        return context;
    }

    public void setEnabled(boolean value) {
        enabled = value;
    }

    public void setMax(int value) {
        max = value;
        maxWrites++;
    }
}
