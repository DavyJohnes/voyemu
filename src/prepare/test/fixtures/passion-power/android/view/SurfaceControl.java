package android.view;

/** Records actual transaction lifecycle and injected platform failures. */
public final class SurfaceControl {
    private SurfaceControl() {}

    public static final class Transaction {
        public static int applied, closed;
        public static boolean failApply, failClose;

        public void apply(boolean synchronous) {
            if (!synchronous) {
                throw new AssertionError("synchronous transaction required");
            }

            applied++;

            if (failApply) {
                throw new IllegalStateException("injected apply failure");
            }
        }

        public void close() {
            closed++;

            if (failClose) {
                throw new IllegalStateException("injected close failure");
            }
        }
    }
}
