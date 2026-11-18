package android.content;

import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;

public abstract class ContentProvider {
    public static Context context;

    public Context getContext() {
        return context;
    }

    public abstract boolean onCreate();

    public Bundle call(String m, String a, Bundle b) {
        return null;
    }

    public abstract Cursor query(Uri u, String[] p, String s, String[] a, String o);

    public abstract String getType(Uri u);

    public abstract Uri insert(Uri u, ContentValues v);

    public abstract int delete(Uri u, String s, String[] a);

    public abstract int update(Uri u, ContentValues v, String s, String[] a);
}
