package android.content;

public class Context {
    public static final String AUDIO_SERVICE = "audio";
    public Object audio;
    public BroadcastReceiver receiver;
    public int registrations, unregistrations;
    private final java.util.List<BroadcastReceiver> receivers = new java.util.ArrayList<>();

    public android.content.res.Resources getResources() {
        return new android.content.res.Resources();
    }

    public Object getSystemService(String name) {
        return audio;
    }

    public Intent registerReceiver(BroadcastReceiver r, IntentFilter f) {
        receivers.add(r);
        receiver = r;
        registrations++;

        return null;
    }

    public void unregisterReceiver(BroadcastReceiver r) {
        if (!receivers.remove(r)) {
            throw new AssertionError();
        }

        receiver = receivers.isEmpty() ? null : receivers.get(receivers.size() - 1);
        unregistrations++;
    }

    public void broadcast() {
        for (BroadcastReceiver r : new java.util.ArrayList<>(receivers)) {
            r.onReceive(this, new Intent());
        }
    }
}
