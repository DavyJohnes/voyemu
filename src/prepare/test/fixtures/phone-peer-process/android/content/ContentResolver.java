package android.content;

public class ContentResolver {
    public android.os.Bundle call(
            android.net.Uri uri, String method, String arg, android.os.Bundle args) {
        if (!"content://com.voyemu.input.phone.peer".equals(uri.toString())) {
            throw new SecurityException("authority");
        }

        return android.os.PeerTestRuntime.call(method, args);
    }
}
