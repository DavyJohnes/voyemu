package android.content;

import android.net.Uri;
import android.os.Bundle;

public class ContentResolver {
    public Bundle result;
    public boolean fail;
    public int calls;

    public Bundle call(Uri u, String method, String arg, Bundle extras) {
        calls++;

        if (!method.equals("getVin") || arg != null || extras != null) {
            throw new AssertionError();
        }

        if (fail) {
            throw new SecurityException();
        }

        return result;
    }
}
