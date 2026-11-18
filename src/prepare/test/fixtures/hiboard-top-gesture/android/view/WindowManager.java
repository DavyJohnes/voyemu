package android.view;

public interface WindowManager {
    class LayoutParams {
        public static final int FLAG_NOT_TOUCHABLE = 16,
                FLAG_NOT_FOCUSABLE = 8,
                FLAG_NOT_TOUCH_MODAL = 32;
        public int type = 2106, flags = 131592, format = -3;
        public float alpha = 1;
        public CharSequence title = "HiboardDelegate";

        public CharSequence getTitle() {
            return title;
        }
    }
}
