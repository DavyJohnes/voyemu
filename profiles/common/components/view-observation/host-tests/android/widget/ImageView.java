package android.widget;

import android.graphics.Matrix;
import android.graphics.drawable.Drawable;
import android.view.View;

public class ImageView extends View {
    public enum ScaleType {
        MATRIX,
        CENTER_CROP
    }

    public Drawable drawable = new Drawable();

    public ScaleType getScaleType() {
        return ScaleType.MATRIX;
    }

    public Matrix getImageMatrix() {
        return new Matrix();
    }

    public Drawable getDrawable() {
        return drawable;
    }

    public void setImageLevel(int level) {
        drawable.setLevel(level);
    }
}
