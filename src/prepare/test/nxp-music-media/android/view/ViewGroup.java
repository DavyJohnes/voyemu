package android.view;

public class ViewGroup extends View {
    public static class LayoutParams {
        public final int width, height;

        public LayoutParams(int width, int height) {
            this.width = width;
            this.height = height;
        }
    }

    public final java.util.List<View> children = new java.util.ArrayList<>();

    public ViewGroup(android.content.Context context) {
        super(context);
    }

    public void addView(View view, LayoutParams params) {
        children.add(view);
        view.parent = this;
        view.width = params.width;
        view.height = params.height;
    }
}
