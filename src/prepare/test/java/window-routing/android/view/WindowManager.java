package android.view;

public interface WindowManager extends ViewManager {
    void removeViewImmediate(View v);

    class InvalidDisplayException extends RuntimeException {}

    class LayoutParams extends ViewGroup.LayoutParams {
        public static final int TYPE_SYSTEM_DIALOG = 2008;
        public static final int TYPE_APPLICATION_OVERLAY = 2038;
        public static final int FLAG_NOT_FOCUSABLE = 8;
        public android.os.IBinder token;
        public int type, flags, x, y;
        CharSequence title;

        public void setTitle(CharSequence s) {
            title = s;
        }

        public CharSequence getTitle() {
            return title;
        }

        public int copyFrom(LayoutParams p) {
            type = p.type;
            token = p.token;
            flags = p.flags;
            x = p.x;
            y = p.y;
            width = p.width;
            height = p.height;
            title = p.title;

            return 0;
        }
    }
}
