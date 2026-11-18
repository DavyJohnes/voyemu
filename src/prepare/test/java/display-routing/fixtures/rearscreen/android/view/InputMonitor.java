package android.view;

public final class InputMonitor {
    public final String name;
    final InputChannel channel;
    public boolean disposed;

    public InputMonitor(String name, int display) {
        this.name = name;
        channel = new InputChannel(display);
    }

    public InputChannel getInputChannel() {
        return channel;
    }

    public void dispose() {
        disposed = true;
    }
}
