package android.os;

/** Live no-op receiver for authority-side adapter tests. */
public final class SettingsTestCallback extends Binder {
    @Override
    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
        return code == 1;
    }
}
