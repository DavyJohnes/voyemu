package android.graphics.drawable;

import android.graphics.Rect;

public class Drawable {
    private int level;
    private int[] state = new int[0];

    public int getIntrinsicWidth() {
        return -1;
    }

    public int getIntrinsicHeight() {
        return 32;
    }

    public Rect getBounds() {
        return new Rect();
    }

    public final boolean setLevel(int level) {
        this.level = level;

        return true;
    }

    public final int getLevel() {
        return level;
    }

    public boolean setState(int[] state) {
        this.state = state;

        return true;
    }

    public int[] getState() {
        return state;
    }
}
