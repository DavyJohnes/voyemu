package android.view;

import android.content.Context;

public class View {
    public interface OnClickListener {
        void onClick(View view);
    }

    public final Context context;
    public int background;
    private OnClickListener click;

    public View(Context context) {
        this.context = context;
    }

    public void setBackgroundColor(int color) {
        background = color;
    }

    public void setOnClickListener(OnClickListener listener) {
        click = listener;
    }

    public boolean performClick() {
        if (click == null) {
            return false;
        }

        click.onClick(this);

        return true;
    }
}
