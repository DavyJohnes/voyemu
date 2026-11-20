package android.view;

import android.content.res.Resources;
import android.graphics.drawable.Drawable;
import android.os.Handler;

public class View {
    public static final int NO_ID = -1;
    public Handler handler = new Handler();
    public boolean enabled = true;
    public ViewGroup.LayoutParams layout;
    public Drawable background;

    public Handler getHandler() {
        return handler;
    }

    public int getMeasuredWidth() {
        return 100;
    }

    public int getMeasuredHeight() {
        return 40;
    }

    public int getMinimumWidth() {
        return 17;
    }

    public int getMinimumHeight() {
        return 19;
    }

    public int getPaddingLeft() {
        return 1;
    }

    public int getPaddingTop() {
        return 2;
    }

    public int getPaddingRight() {
        return 3;
    }

    public int getPaddingBottom() {
        return 4;
    }

    public int getScrollX() {
        return -5;
    }

    public int getScrollY() {
        return 6;
    }

    public ViewGroup.LayoutParams getLayoutParams() {
        return layout;
    }

    public int getId() {
        return 7;
    }

    public Resources getResources() {
        return new Resources();
    }

    public int getLeft() {
        return 2;
    }

    public int getTop() {
        return 3;
    }

    public int getRight() {
        return 102;
    }

    public int getBottom() {
        return 43;
    }

    public void getLocationOnScreen(int[] p) {
        p[0] = 20;
        p[1] = 30;
    }

    public float getScaleX() {
        return 1;
    }

    public float getScaleY() {
        return 1;
    }

    public float getRotation() {
        return 0;
    }

    public float getRotationX() {
        return 0;
    }

    public float getRotationY() {
        return 0;
    }

    public float getAlpha() {
        return 1;
    }

    public int getVisibility() {
        return 0;
    }

    public boolean isEnabled() {
        return enabled;
    }

    public boolean isClickable() {
        return true;
    }

    public boolean isLongClickable() {
        return false;
    }

    public boolean isFocusable() {
        return true;
    }

    public boolean isFocused() {
        return false;
    }

    public boolean isSelected() {
        return true;
    }

    public Drawable getBackground() {
        return background;
    }
}
