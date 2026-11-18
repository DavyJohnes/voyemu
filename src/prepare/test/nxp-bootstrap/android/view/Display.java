package android.view;

public class Display {
    public static final int DEFAULT_DISPLAY = 0;
    private final int id;

    public Display(int id) {
        this.id = id;
    }

    public int getDisplayId() {
        return id;
    }

    public String getName() {
        return id == 0 ? "Default" : "Emulator2D";
    }

    public boolean isValid() {
        return true;
    }
}
