package android.view;

public class ViewGroup extends View {
    public static class LayoutParams {
        public int width = -1, height = -2;
    }

    public static class MarginLayoutParams extends LayoutParams {
        public int leftMargin = 7, topMargin = 8, rightMargin = 9, bottomMargin = 10;
    }
}
