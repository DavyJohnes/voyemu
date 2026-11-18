package android.view;

public class Display {
    public static final int DEFAULT_DISPLAY = 0;
    final int id;

    final String name;

    public Display(int i, String n) {
        id = i;
        name = n;
    }

    public int getDisplayId() {
        return id;
    }

    public String getName() {
        return name;
    }

    public boolean isValid() {
        return true;
    }
}
