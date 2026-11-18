package android.os;

import java.util.ArrayList;
import java.util.List;

public class RemoteCallbackList<E extends IInterface> {
    private final List<E> callbacks = new ArrayList<>();
    private List<E> broadcast;

    public boolean register(E callback) {
        return !callbacks.contains(callback) && callbacks.add(callback);
    }

    public boolean unregister(E callback) {
        return callbacks.remove(callback);
    }

    public int beginBroadcast() {
        if (broadcast != null) {
            throw new IllegalStateException("beginBroadcast() called while already in a broadcast");
        }

        broadcast = new ArrayList<>(callbacks);

        return broadcast.size();
    }

    public E getBroadcastItem(int index) {
        return broadcast.get(index);
    }

    public void finishBroadcast() {
        broadcast = null;
    }
}
