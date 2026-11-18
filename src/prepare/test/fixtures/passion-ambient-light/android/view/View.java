package android.view;

public class View {
    public boolean enabled = true;
    public final java.util.Map<Integer, View> children = new java.util.HashMap<>();

    public View findViewById(int id) {
        return children.get(id);
    }

    public boolean isEnabled() {
        return enabled;
    }

    public void setEnabled(boolean value) {
        enabled = value;
    }
}
