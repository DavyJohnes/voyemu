package android.app;

public final class IActivityManager {
    public Object stacks;
    public Object focused;
    public boolean fail;

    public Object getAllStackInfos() {
        if (fail) {
            throw new IllegalStateException("stack service failed");
        }

        return stacks;
    }

    public Object getFocusedStackInfo() {
        if (fail) {
            throw new IllegalStateException("focus service failed");
        }

        return focused;
    }

    public static final class StackInfo {
        public int displayId;
        public boolean visible;
        public int position;
        public Object topActivity;

        public StackInfo(int displayId, boolean visible, int position, Object topActivity) {
            this.displayId = displayId;
            this.visible = visible;
            this.position = position;
            this.topActivity = topActivity;
        }
    }
}
