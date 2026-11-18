package android.content;

public class Intent {
    public static final int FLAG_ACTIVITY_NEW_TASK = 0x10000000;
    private ComponentName component;
    private String action;
    private int flags;

    public Intent() {}

    public Intent(String action) {
        this.action = action;
    }

    public Intent setComponent(ComponentName value) {
        component = value;

        return this;
    }

    public ComponentName getComponent() {
        return component;
    }

    public Intent addFlags(int value) {
        flags |= value;

        return this;
    }

    public String getAction() {
        return action;
    }

    public int getFlags() {
        return flags;
    }
}
