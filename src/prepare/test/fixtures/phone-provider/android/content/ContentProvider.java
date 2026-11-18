package android.content;

public abstract class ContentProvider {
    private Context c = new Context();

    public Context getContext() {
        return c;
    }

    public abstract boolean onCreate();

    public abstract android.os.Bundle call(String a, String b, android.os.Bundle c);

    public abstract android.database.Cursor query(
            android.net.Uri a, String[] b, String c, String[] d, String e);

    public abstract String getType(android.net.Uri u);

    public abstract android.net.Uri insert(android.net.Uri u, ContentValues v);

    public abstract int delete(android.net.Uri a, String b, String[] c);

    public abstract int update(android.net.Uri a, ContentValues b, String c, String[] d);
}
