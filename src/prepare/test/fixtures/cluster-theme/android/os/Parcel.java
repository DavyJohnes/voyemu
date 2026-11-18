package android.os;

import java.util.ArrayList;
import java.util.List;

public class Parcel {
    private final List<Object> data = new ArrayList<>();
    private int at;

    public static Parcel obtain() {
        return new Parcel();
    }

    public void recycle() {}

    public void writeInterfaceToken(String s) {
        writeString(s);
    }

    public void enforceInterface(String s) {
        if (!s.equals(readString())) {
            throw new SecurityException();
        }
    }

    public void writeString(String s) {
        data.add(s);
    }

    public String readString() {
        return (String) data.get(at++);
    }

    public void writeInt(int i) {
        data.add(i);
    }

    public int readInt() {
        return (Integer) data.get(at++);
    }

    public void writeFloat(float f) {
        data.add(f);
    }

    public float readFloat() {
        return (Float) data.get(at++);
    }

    public void writeBundle(Bundle b) {
        data.add(b);
    }

    public Bundle readBundle() {
        return (Bundle) data.get(at++);
    }

    public void writeStrongBinder(IBinder b) {
        data.add(b);
    }

    public IBinder readStrongBinder() {
        return (IBinder) data.get(at++);
    }

    public void writeNoException() {
        writeInt(0);
    }

    public void readException() {
        if (readInt() != 0) {
            throw new RuntimeException();
        }
    }
}
