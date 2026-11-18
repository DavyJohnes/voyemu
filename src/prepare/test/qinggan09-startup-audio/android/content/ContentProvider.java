package android.content;

public abstract class ContentProvider {
    public Context getContext() {
        throw new AssertionError("provider boundary not attached");
    }

    public abstract boolean onCreate();

    public abstract android.os.Bundle call(String method, String arg, android.os.Bundle extras);

    public abstract android.database.Cursor query(
            android.net.Uri uri, String[] projection, String selection, String[] args, String sort);

    public abstract String getType(android.net.Uri uri);

    public abstract android.net.Uri insert(android.net.Uri uri, ContentValues values);

    public abstract int delete(android.net.Uri uri, String where, String[] args);

    public abstract int update(
            android.net.Uri uri, ContentValues values, String where, String[] args);
}
