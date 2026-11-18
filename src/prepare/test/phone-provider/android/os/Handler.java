package android.os;

public final class Handler {
    public Handler(Looper loop) {}

    // Mirrors android.os.Handler; this test must never reach the main loop.
    @SuppressWarnings("DoNotCallSuggester")
    public boolean post(Runnable r) {
        throw new AssertionError("unexpected main-loop call in boundary-isolated protocol test");
    }
}
