package android.system;

public final class ErrnoException extends Exception {
    public final int errno;

    public ErrnoException(int errno) {
        super("must never enter diagnostics");
        this.errno = errno;
    }
}
