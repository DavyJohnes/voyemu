package android.os;

public class Bundle {
    public String value;

    public String getString(String key) {
        if (!key.equals("vin")) {
            throw new AssertionError();
        }

        return value;
    }
}
