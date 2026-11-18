package android.os;

public class Binder implements IBinder {
    @Override
    public void linkToDeath(DeathRecipient recipient, int flags) {}

    @Override
    public boolean unlinkToDeath(DeathRecipient recipient, int flags) {
        return true;
    }
}
