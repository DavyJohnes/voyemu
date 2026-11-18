package android.os;

import java.util.ArrayList;
import java.util.List;

public class Parcel {
    private final List<Object> values = new ArrayList<Object>();
    private int at;

    public static Parcel obtain() {
        return new Parcel();
    }

    public void recycle() {}

    public void writeInterfaceToken(String token) {
        writeString(token);
    }

    public void enforceInterface(String expected) {
        if (!expected.equals(readString())) {
            throw new SecurityException("token");
        }
    }

    public void writeString(String value) {
        values.add(value);
    }

    public String readString() {
        return (String) values.get(at++);
    }

    public void writeInt(int value) {
        values.add(value);
    }

    public int readInt() {
        return (Integer) values.get(at++);
    }

    public int dataAvail() {
        return values.size() - at;
    }
}
