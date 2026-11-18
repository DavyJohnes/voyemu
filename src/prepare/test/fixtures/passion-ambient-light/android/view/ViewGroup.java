package android.view;

public class ViewGroup extends View {
    public final java.util.List<View> items = new java.util.ArrayList<>();

    public int getChildCount() {
        return items.size();
    }

    public View getChildAt(int i) {
        return items.get(i);
    }
}
