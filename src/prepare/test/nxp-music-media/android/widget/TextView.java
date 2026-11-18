package android.widget;

public class TextView extends android.view.View {
    public CharSequence text;

    public TextView(android.content.Context context) {
        super(context);
    }

    public void setText(CharSequence value) {
        text = value;
    }

    public void setTextColor(int value) {}

    public void setTextSize(float value) {}

    public void setGravity(int value) {}
}
